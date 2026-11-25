#include "app/StudioController.h"

#include <QMetaObject>
#include <algorithm>
#include <utility>

using L = LabController;
using State = StudioTake::State;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QStringList list(const std::vector<std::string>& v) {
  QStringList l;
  for (const auto& s : v) l << q(s);
  return l;
}
QString takeState(State s) {
  switch (s) {
    case State::Pending: return StudioController::tr("pending");
    case State::Primary: return StudioController::tr("primary");
    case State::Alternate: return StudioController::tr("alternate");
    case State::Rejected: return StudioController::tr("rejected");
    case State::Withdrawn: return StudioController::tr("Consent withdrawn – delete or replace");  // D-133
  }
  return {};
}
int takeLevel(State s) {
  switch (s) {
    case State::Primary:
    case State::Alternate: return L::Ok;
    case State::Pending: return L::Caution;
    case State::Withdrawn: return L::Fault;
    case State::Rejected: break;
  }
  return L::None;
}
}  // namespace

StudioController::StudioController(AccessController* access, QObject* parent) : QObject(parent) { setAccess(access); }

void StudioController::setAccess(AccessController* a) {
  if (access_) access_->disconnect(this);
  access_ = a;
  if (a) connect(a, &AccessController::changed, this, &StudioController::accessChanged);
  accessChanged();
}

void StudioController::setBackend(StudioBackend b) {
  b_ = std::move(b);
  if (!b_.setEvents) return;
  StudioEvents e;
  e.level = [this](double db) {
    queued([this, db] {
      level_ = db;
      emit levelChanged();
    });
  };
  e.elapsed = [this](int ms) {
    queued([this, ms] {
      elapsedMs_ = ms;
      emit levelChanged();
    });
  };
  e.roomTone = [this](double db, const std::string& warning) {
    queued([this, db, warning] {
      roomTone_ = db;
      roomWarning_ = q(warning);
    });
  };
  e.recording = [this](bool on) {
    queued([this, on] {
      recording_ = on;
      if (on) elapsedMs_ = 0;
      else level_ = -120;
      emit sessionChanged();
      emit levelChanged();
    });
  };
  e.recordingStopped = [this](const std::string& why) { queued([this, why] { if (!why.empty()) say(L::Caution, q(why)); }); };
  e.takeReady = [this](StudioTake t) {
    queued([this, t = std::move(t)] {
      for (StudioClip& c : clips_) {
        if (c.id != t.clipId) continue;
        const auto at = std::find_if(c.takes.begin(), c.takes.end(), [&](const StudioTake& x) { return x.id == t.id; });
        if (at == c.takes.end()) c.takes.insert(c.takes.begin(), t);
        else *at = t;
      }
      if (t.clipId == clipId_) takeId_ = t.id;
      loadPeaks();
      emit clipChanged();
      emit stripChanged();
      emit coverageChanged();
    });
  };
  e.sessionEnded = [this](const std::string& why) { queued([this, why] { if (session_) dropSession(q(why), false); }); };
  e.clipsChanged = [this] { queued([this] { loadClips(); }); };
  b_.setEvents(std::move(e));
}

void StudioController::queued(std::function<void()> f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }

bool StudioController::say(int level, const QString& text) {
  messageLevel_ = text.isEmpty() ? int(L::None) : level;
  message_ = text;
  emit messageChanged();
  return false;
}

StudioBackend::Done StudioController::answer(std::function<void()> ok, const QString& doneText) {
  ++waiting_;
  emit sessionChanged();
  emit entryChanged();
  return [this, ok = std::move(ok), doneText](const std::string& why) {
    queued([this, ok, doneText, why] {
      --waiting_;
      if (!why.empty()) {
        say(L::Caution, q(why));
      } else {
        say(L::Ok, doneText);
        if (ok) ok();
      }
      emit sessionChanged();
      emit entryChanged();
    });
  };
}

bool StudioController::allowed() {
  if (!connected()) return say(L::Caution, tr("This build has no Studio backend."));
  if (!instructor()) return say(L::Caution, tr("Only the instructor may do this."));
  if (busy()) return say(L::Caution, tr("Wait for the last request."));
  return true;
}

void StudioController::allChanged() {
  emit listChanged();
  emit entryChanged();
  emit stripChanged();
  emit clipChanged();
  emit sessionChanged();
  emit coverageChanged();
}

// ---- role and consent

QString StudioController::refusal(const QString& id) const {
  if (access_)
    for (const QVariant& v : access_->people()) {
      const QVariantMap p = v.toMap();
      if (p["id"].toString() != id) continue;
      if (p["again"].toBool() || p["pilotAgain"].toBool()) return tr("re-consent needed");  // D-135
      if (!p["consent"].toBool()) return tr("no consent to be recorded");
      if (!p["pilot"].toBool() || p["pilotDate"].toString().isEmpty()) return tr("no consent for the pilot voice");
      return {};
    }
  return tr("no trainee record");
}

bool StudioController::mayUse(const QString& id) const {  // D-134: the pilot-voice answer alone
  if (access_)
    for (const QVariant& v : access_->people()) {
      const QVariantMap p = v.toMap();
      if (p["id"].toString() == id) return p["pilot"].toBool() && !p["pilotDate"].toString().isEmpty() && !p["pilotAgain"].toBool();
    }
  return false;
}

QVariantList StudioController::speakers() const {
  QVariantList l;
  if (!instructor()) return l;
  for (const QVariant& v : access_->people()) {
    const QString id = v.toMap()["id"].toString(), why = refusal(id);
    l.append(QVariantMap{{"id", id}, {"may", why.isEmpty()}, {"why", why}});
  }
  return l;
}

void StudioController::accessChanged() {
  const bool now = instructor();
  if (std::exchange(wasInstructor_, now) != now) {
    if (now) {
      reload();
    } else {
      if (session_ || starting_) dropSession(tr("The instructor logged out: the recording session has ended."), true);
      stopPlay();
      loaded_ = false;
    }
    allChanged();
    return;
  }
  if (session_) {
    if (const QString why = refusal(speaker_); !why.isEmpty())
      dropSession(tr("%1: %2. The recording session has ended.").arg(speaker_, why), true);
  }
  emit sessionChanged();
}

void StudioController::setTrainerBusy(bool on) {
  if (trainerBusy_ == on) return;
  trainerBusy_ = on;
  if (on && (session_ || starting_)) dropSession(tr("An exercise has started: the recording session has ended."), true);
  emit sessionChanged();
}

void StudioController::dropSession(const QString& why, bool tellBackend) {
  ++startNo_;
  if (pedal_ && b_.setPedal) b_.setPedal(false, {});
  session_ = starting_ = pedal_ = false;  // recording_ follows the backend
  speaker_.clear();
  if (tellBackend && b_.endSession) b_.endSession();  // keeps an open take as pending
  say(L::Caution, why);
  emit sessionChanged();
  emit levelChanged();
}

// ---- loading

void StudioController::enter() {
  clearMessage();
  if (instructor()) reload();
}

void StudioController::leave() {
  if (session_ || starting_) dropSession({}, true);
  stopPlay();
}

void StudioController::reload(bool reopen) {
  if (!b_.load) return;
  const unsigned n = ++loadNo_;
  b_.load([this, n, reopen](StudioContent c, const std::string& why) {
    queued([this, n, reopen, c = std::move(c), why] {
      if (n != loadNo_ || !instructor()) return;
      if (!why.empty()) {
        loaded_ = false;
        say(L::Fault, q(why));
        emit listChanged();
        return;
      }
      const bool keep = loaded_ && !reopen && dirty();  // an edit stays, with the revision it started from
      content_ = c;
      loaded_ = true;
      if (scenario_.empty() && !content_.scenarios.empty()) scenario_ = content_.scenarios.front().id;
      if (!keep) {
        const StudioEntry* e = savedEntry();
        if (!e) {
          const auto first = std::find_if(content_.entries.begin(), content_.entries.end(), [&](const StudioEntry& x) { return x.reply == replies_; });
          if (first != content_.entries.end()) e = &*first;
        }
        if (e) open(*e);
      }
      allChanged();
      loadClips();
    });
  });
}

void StudioController::loadClips() {
  if (!b_.clips || scenario_.empty() || !instructor()) return;
  const unsigned n = ++clipsNo_;
  b_.clips(scenario_, [this, n](std::vector<StudioClip> c, const std::string& why) {
    queued([this, n, c = std::move(c), why] {
      if (n != clipsNo_) return;
      if (!why.empty()) say(L::Fault, q(why));
      clips_ = c;
      if (!current()) {
        const StudioClip* sel = find(clipId_);
        takeId_ = sel && !sel->takes.empty() ? sel->takes.front().id : std::string();
      }
      loadPeaks();
      emit listChanged();
      emit stripChanged();
      emit clipChanged();
      emit sessionChanged();
      emit coverageChanged();
      if (!hasClip() && !recording_ && preview_) selectSegment(std::max(0, segment_));
    });
  });
}

// ---- list

int StudioController::count(bool reply) const {
  return int(std::count_if(content_.entries.begin(), content_.entries.end(), [&](const StudioEntry& e) { return e.reply == reply; }));
}

const StudioEntry* StudioController::savedEntry() const {
  for (const StudioEntry& e : content_.entries)
    if (e.id == draft_.id && e.reply == draft_.reply) return &e;
  return nullptr;
}

QVariantList StudioController::entries() const {
  QVariantList l;
  if (!ready()) return l;
  for (const StudioEntry& e : content_.entries) {
    if (e.reply != replies_) continue;
    const bool hit = search_.isEmpty() || q(e.id).contains(search_, Qt::CaseInsensitive) ||
                     std::any_of(e.texts.begin(), e.texts.end(), [&](const std::string& t) { return q(t).contains(search_, Qt::CaseInsensitive); });
    if (!hit) continue;
    int needed = 0, found = 0;
    for (const StudioClip& c : clips_) {
      if (!c.needed || std::find(c.replies.begin(), c.replies.end(), e.id) == c.replies.end()) continue;
      ++needed;
      found += stateOf(c) != ClipState::Missing;
    }
    l.append(QVariantMap{{"id", q(e.id)}, {"status", e.validated ? tr("Validated") : tr("Draft")}, {"validated", e.validated},
                         {"coverage", !e.reply ? QString() : needed ? tr("%1 of %2 clips").arg(found).arg(needed) : tr("not used")},
                         {"complete", needed > 0 && found == needed}});
  }
  return l;
}

void StudioController::showReplies(bool on) {
  if (on == replies_) return;
  if (recording_) return void(say(L::Caution, tr("Stop the recording first.")));
  if (dirty()) return void(say(L::Caution, tr("Save or undo the changes to %1 first.").arg(entryId())));
  replies_ = on;
  search_.clear();
  clearMessage();
  for (const StudioEntry& e : content_.entries)
    if (e.reply == on) {
      open(e);
      break;
    }
  emit listChanged();
  emit clipChanged();
  emit sessionChanged();
}

void StudioController::search(const QString& words) {
  if (search_ == words) return;
  search_ = words;
  emit listChanged();
}

void StudioController::select(const QString& id) {
  if (id == entryId()) return;
  if (recording_) return void(say(L::Caution, tr("Stop the recording first.")));
  if (dirty()) return void(say(L::Caution, tr("Save or undo the changes to %1 first.").arg(entryId())));
  for (const StudioEntry& e : content_.entries)
    if (e.reply == replies_ && q(e.id) == id) {
      clearMessage();
      open(e);
      return;
    }
}

void StudioController::open(const StudioEntry& e) {
  draft_ = e;
  if (draft_.texts.empty()) draft_.texts.emplace_back();
  draftRevision_ = content_.revision;
  example_ = 0;
  segment_ = -1;
  preview_.reset();
  emit entryChanged();
  emit entryLoaded();
  emit stripChanged();
  preview();
}

// ---- editor

QString StudioController::statusNote() const {
  if (draft_.id.empty()) return {};
  if (draft_.placeholderFile) return tr("PLACEHOLDER file: cannot be Validated");
  if (draft_.validated && !draft_.validatedAt.empty()) return tr("by %1, %2").arg(q(draft_.validatedBy), q(draft_.validatedAt));
  return {};
}

QString StudioController::usedBy() const { return draft_.usedBy.empty() ? tr("not used by a scenario") : list(draft_.usedBy).join(QStringLiteral("; ")); }

QString StudioController::text() const { return example_ < int(draft_.texts.size()) ? q(draft_.texts[std::size_t(example_)]) : QString(); }

bool StudioController::dirty() const {
  const StudioEntry* s = savedEntry();
  return s && (s->texts != draft_.texts || s->validated != draft_.validated || s->sopRef != draft_.sopRef);
}

bool StudioController::canSave() const {
  return dirty() && preview_ && preview_->error.empty() && !busy() && instructor() &&
         std::none_of(draft_.texts.begin(), draft_.texts.end(), [](const std::string& t) { return q(t).trimmed().isEmpty(); });
}

void StudioController::preview() {
  if (!b_.preview || draft_.id.empty()) return;
  const unsigned n = ++previewNo_;
  b_.preview(draft_, example_, [this, n](StudioPreview p) {
    queued([this, n, p = std::move(p)] {
      if (n != previewNo_) return;
      preview_ = p;
      if (segment_ >= int(p.segments.size())) segment_ = -1;
      if (segment_ < 0 && !recording_) {
        segment_ = segmentOf(clipId_);
        if (segment_ < 0 && !p.segments.empty()) selectSegment(0);
      }
      emit stripChanged();
      emit entryChanged();
    });
  });
}

void StudioController::selectExample(int i) {
  if (draft_.reply || i < 0 || i >= exampleCount() || i == example_) return;
  example_ = i;
  segment_ = -1;
  preview_.reset();
  emit entryChanged();
  emit entryLoaded();
  emit stripChanged();
  preview();
}

void StudioController::addExample() {
  if (draft_.reply || !allowed()) return;
  draft_.texts.emplace_back();
  draft_.validated = false;
  selectExample(exampleCount() - 1);
}

void StudioController::setText(const QString& t) {
  if (example_ >= int(draft_.texts.size()) || t == text() || !instructor()) return;
  draft_.texts[std::size_t(example_)] = t.toStdString();
  draft_.validated = false;  // D-25: editing returns it to Draft
  emit entryChanged();
  emit stripChanged();
  preview();
}

void StudioController::setSopRef(const QString& t) {
  if (t == sopRef() || !instructor()) return;
  draft_.sopRef = t.toStdString();
  if (t.trimmed().isEmpty()) draft_.validated = false;
  emit entryChanged();
}

void StudioController::setValidated(bool on) {
  if (on == draft_.validated || !allowed()) return;
  if (on && draft_.placeholderFile) return void(say(L::Caution, tr("The file is marked PLACEHOLDER. Its texts cannot be Validated.")));
  const StudioEntry* saved = savedEntry();
  if (on && saved && saved->texts != draft_.texts)  // a changed text is always saved as Draft (D-332)
    return void(say(L::Caution, tr("Save the changed text first. Then set it to Validated.")));
  if (on && sopRef().trimmed().isEmpty()) return void(say(L::Caution, tr("Enter the SOP reference first.")));
  clearMessage();
  draft_.validated = on;
  emit entryChanged();
}

void StudioController::save() {
  if (!canSave() || !allowed() || !b_.save) return;
  b_.save(draft_, draftRevision_, answer([this] {
            reload(true);
            emit saved();
          }, tr("Saved on this machine.")));
}

void StudioController::undo() {
  if (const StudioEntry* s = savedEntry()) {
    clearMessage();
    const StudioEntry copy = *s;
    open(copy);
  }
}

void StudioController::addReply(const QString& id, const QString& text) {
  if (!allowed() || !b_.addReply) return;
  if (dirty()) return void(say(L::Caution, tr("Save or undo the changes to %1 first.").arg(entryId())));
  StudioEntry e;
  e.id = id.trimmed().toStdString();
  e.texts = {text.trimmed().toStdString()};
  if (e.id.empty() || e.texts.front().empty()) return void(say(L::Caution, tr("ID and text are required.")));
  if (std::any_of(content_.entries.begin(), content_.entries.end(), [&](const StudioEntry& x) { return x.reply && x.id == e.id; }))
    return void(say(L::Caution, tr("%1 exists already.").arg(q(e.id))));
  b_.addReply(e, content_.revision, answer([this, e] {
                draft_.id = e.id;
                draft_.reply = replies_ = true;
                reload(true);
                emit added();
              }, tr("Added as Draft.")));
}

QVariantList StudioController::segments() const {
  QVariantList l;
  if (!preview_) return l;
  for (const StudioSegment& s : preview_->segments) {
    QString state;
    if (draft_.reply) {
      if (!s.dynamic) {
        const StudioClip* c = find(s.clipId);
        state = letter(c ? stateOf(*c) : ClipState::Missing);
      } else {  // the worst of the pool's values
        const auto pool = poolOf(s);
        ClipState worst = pool.empty() ? ClipState::Missing : ClipState::Primary;
        for (const StudioClip* c : pool) {
          if (stateOf(*c) == ClipState::Missing) worst = ClipState::Missing;
          else if (stateOf(*c) == ClipState::Alternate && worst == ClipState::Primary) worst = ClipState::Alternate;
        }
        state = letter(worst);
      }
    }
    l.append(QVariantMap{{"text", s.dynamic ? QStringLiteral("{%1}").arg(q(s.text)) : q(s.text)}, {"dynamic", s.dynamic},
                         {"position", s.position == "initial" ? tr("INI") : s.position == "medial" ? tr("MED") : s.position == "final" ? tr("FIN") : QString()},
                         {"comma", s.commaAfter}, {"state", state}});
  }
  return l;
}

QStringList StudioController::lint() const {
  if (!preview_) return {};
  if (!preview_->error.empty()) return {q(preview_->error)};
  return list(preview_->lint);
}

QString StudioController::changeText() const {
  if (!dirty()) return tr("No changes");
  if (!preview_) return tr("Checking");
  if (!preview_->error.empty()) return tr("The text cannot be read");
  if (preview_->missing.empty() && preview_->unused.empty()) return tr("Changed, the clips stay the same");
  return tr("Wording changed: %1 missing, %2 unused").arg(preview_->missing.size()).arg(preview_->unused.size());
}

QStringList StudioController::changeRows() const {
  QStringList l;
  if (!preview_) return l;
  for (const auto& m : preview_->missing) l << tr("missing  %1").arg(q(m));
  for (const auto& u : preview_->unused) l << tr("unused   %1").arg(q(u));
  return l;
}

// ---- clips

StudioController::ClipState StudioController::stateOf(const StudioClip& c) {
  const auto has = [&](State s) { return std::any_of(c.takes.begin(), c.takes.end(), [s](const StudioTake& t) { return t.state == s; }); };
  return has(State::Primary) ? ClipState::Primary : has(State::Alternate) ? ClipState::Alternate : ClipState::Missing;
}

QString StudioController::letter(ClipState s) {
  return s == ClipState::Primary ? QStringLiteral("P") : s == ClipState::Alternate ? QStringLiteral("A") : QStringLiteral("M");
}

const StudioClip* StudioController::find(const std::string& id) const {
  if (id.empty()) return nullptr;
  for (const StudioClip& c : clips_)
    if (c.id == id) return &c;
  return nullptr;
}

const StudioTake* StudioController::current() const {
  if (const StudioClip* c = find(clipId_))
    for (const StudioTake& t : c->takes)
      if (t.id == takeId_) return &t;
  return nullptr;
}

std::vector<const StudioClip*> StudioController::poolOf(const StudioSegment& s) const {
  std::vector<const StudioClip*> l;
  for (const StudioClip& c : clips_)
    if (s.dynamic && !s.pool.empty() && c.pool == s.pool && c.position == s.position) l.push_back(&c);
  return l;
}

int StudioController::segmentOf(const std::string& clip) const {
  const StudioClip* c = find(clip);
  if (!c || !preview_ || !draft_.reply) return -1;
  for (std::size_t i = 0; i < preview_->segments.size(); ++i) {
    const StudioSegment& s = preview_->segments[i];
    if (s.dynamic ? s.pool == c->pool && s.position == c->position && !c->pool.empty() : s.clipId == clip) return int(i);
  }
  return -1;
}

void StudioController::selectSegment(int i) {
  if (!preview_ || i < 0 || i >= int(preview_->segments.size())) return;
  if (recording_) return void(say(L::Caution, tr("Stop the recording first.")));
  segment_ = i;
  const StudioSegment& s = preview_->segments[std::size_t(i)];
  if (draft_.reply) {
    const auto pool = poolOf(s);
    const bool inPool = std::any_of(pool.begin(), pool.end(), [this](const StudioClip* c) { return c->id == clipId_; });
    if (!s.dynamic) selectClip(q(s.clipId));
    else if (!inPool) selectClip(pool.empty() ? QString() : q(pool.front()->id));
  }
  emit stripChanged();
}

void StudioController::selectClip(const QString& id) {
  if (recording_) return void(say(L::Caution, tr("Stop the recording first.")));
  stopPlay();
  clipId_ = id.toStdString();
  const StudioClip* c = find(clipId_);
  takeId_ = c && !c->takes.empty() ? c->takes.front().id : std::string();
  if (pedal_ && b_.setPedal) b_.setPedal(true, clipId_);
  loadPeaks();
  emit clipChanged();
  emit stripChanged();
  emit sessionChanged();
}

void StudioController::selectTake(int row) {
  const StudioClip* c = find(clipId_);
  if (!c || recording_ || row < 0 || row >= int(c->takes.size())) return;
  stopPlay();
  takeId_ = c->takes[std::size_t(row)].id;
  loadPeaks();
  emit clipChanged();
}

// The raw take does not change when it is trimmed, so its waveform is asked for once.
void StudioController::loadPeaks() {
  const StudioTake* t = current();
  const std::string id = t && t->state != State::Withdrawn ? t->id : std::string();
  if (id == peaksOf_) return;
  peaksOf_ = id;
  peaks_.clear();
  const unsigned n = ++peaksNo_;
  if (id.empty() || !b_.peaks) return;
  b_.peaks(id, [this, n](std::vector<float> p, const std::string& why) {
    queued([this, n, p = std::move(p), why] {
      if (n != peaksNo_) return;
      if (!why.empty()) say(L::Caution, q(why));
      peaks_ = p;
      emit clipChanged();
    });
  });
}

QString StudioController::poolTitle() const {
  if (!preview_ || !draft_.reply || segment_ < 0 || segment_ >= int(preview_->segments.size())) return {};
  const StudioSegment& s = preview_->segments[std::size_t(segment_)];
  if (!s.dynamic) return {};
  for (const StudioScenario& sc : content_.scenarios)
    if (sc.id == scenario_) return tr("Pool %1 (%2)").arg(q(s.pool), q(sc.airfield));
  return tr("Pool %1").arg(q(s.pool));
}

QVariantList StudioController::poolClips() const {
  QVariantList l;
  if (!preview_ || !draft_.reply || segment_ < 0 || segment_ >= int(preview_->segments.size())) return l;
  for (const StudioClip* c : poolOf(preview_->segments[std::size_t(segment_)]))
    l.append(QVariantMap{{"id", q(c->id)}, {"value", q(c->value)}, {"state", letter(stateOf(*c))}, {"selected", c->id == clipId_}});
  return l;
}

QVariantMap StudioController::clip() const {
  const StudioClip* c = find(clipId_);
  if (!c) return {};
  return {{"id", q(c->id)}, {"words", q(c->words)}, {"before", q(c->sayBefore)}, {"target", q(c->sayTarget)}, {"after", q(c->sayAfter)},
          {"used", c->needed ? tr("Used by %1 of %2 replies").arg(c->replies.size()).arg(replyCount()) : tr("Unused in %1").arg(scenario())},
          {"differs", !c->bankWords.empty() && c->bankWords != c->words ? tr("The clip says \"%1\"").arg(q(c->bankWords)) : QString()}};
}

QVariantList StudioController::takes() const {
  QVariantList l;
  const StudioClip* c = find(clipId_);
  if (!c) return l;
  for (const StudioTake& t : c->takes)
    l.append(QVariantMap{{"number", t.number}, {"who", t.synthetic ? tr("synthetic") : q(t.speaker)}, {"state", takeState(t.state)},
                         {"level", takeLevel(t.state)}, {"info", tr("%1 s  %2 dBFS").arg(t.seconds, 0, 'f', 2).arg(t.levelDb, 0, 'f', 0)},
                         {"selected", t.id == takeId_}});
  return l;
}

QVariantMap StudioController::take() const {
  const StudioTake* t = current();
  if (!t) return {};
  QVariantList peaks;
  if (t->id == peaksOf_)
    for (float p : peaks_) peaks.append(double(p));
  const bool gone = t->state == State::Withdrawn;
  return {{"start", t->trimStart}, {"end", t->trimEnd}, {"peaks", peaks}, {"warnings", list(t->warnings).join(QStringLiteral("; "))},
          {"withdrawn", gone}, {"removable", gone || t->state == State::Rejected}, {"decidable", !gone},
          {"trimmable", t->state == State::Pending}, {"number", t->number}};
}

// ---- recording

void StudioController::startSession(const QString& speaker) {
  if (session_ || starting_ || !allowed() || !b_.startSession) return;
  if (trainerBusy_) return void(say(L::Caution, tr("Not during an exercise.")));
  if (const QString why = refusal(speaker); !why.isEmpty()) return void(say(L::Caution, tr("%1 may not record: %2.").arg(speaker, why)));
  const unsigned n = ++startNo_;
  starting_ = true;
  roomTone_ = -120;
  roomWarning_.clear();
  say(L::None, {});
  b_.startSession(speaker.toStdString(), [this, n, done = answer([this, speaker, n] {
                    if (n != startNo_) return;
                    starting_ = false;
                    // The answer may have changed while the backend worked.
                    if (!instructor() || trainerBusy_ || !refusal(speaker).isEmpty()) {
                      dropSession(tr("The recording session was not started."), true);
                      return;
                    }
                    session_ = true;
                    speaker_ = speaker;
                    if (!roomWarning_.isEmpty()) say(L::Caution, roomWarning_);
                    else say(L::Ok, roomTone_ > -120 ? tr("Recording session started. Room tone %1 dBFS.").arg(roomTone_, 0, 'f', 0) : tr("Recording session started."));
                    emit sessionChanged();
                  })](const std::string& why) {
    queued([this, n, why] {
      if (n == startNo_) {
        if (!why.empty()) starting_ = false;
      } else if (why.empty() && !session_ && b_.endSession) {
        b_.endSession();  // started after it had been left
      }
    });
    done(why);
  });
}

void StudioController::endSession() {
  if (session_ || starting_) dropSession({}, true);
}

void StudioController::setPedal(bool on) {
  if (!session_ || on == pedal_) return;
  pedal_ = on;
  if (b_.setPedal) b_.setPedal(on, on ? clipId_ : std::string());
  emit sessionChanged();
}

void StudioController::toggleRecord() {
  if (recording_) {
    if (b_.stopRecord) b_.stopRecord();  // recordingStopped, then takeReady
    return;
  }
  if (!allowed()) return;
  if (!replies_) return void(say(L::Caution, tr("ATC examples are not recorded.")));
  if (!session_) return void(say(L::Caution, tr("Start a recording session first.")));
  if (!hasClip()) return void(say(L::Caution, tr("Choose a clip first.")));
  if (const QString why = refusal(speaker_); !why.isEmpty()) return void(say(L::Caution, tr("%1 may not record: %2.").arg(speaker_, why)));
  stopPlay();
  b_.record(clipId_, answer({}));  // recording() follows when it really starts
}

QString StudioController::elapsed() const {
  return QStringLiteral("%1:%2.%3").arg(elapsedMs_ / 60000, 2, 10, QLatin1Char('0')).arg(elapsedMs_ / 1000 % 60, 2, 10, QLatin1Char('0')).arg(elapsedMs_ / 100 % 10);
}

void StudioController::setTrim(double start, double end) {
  const StudioTake* t = current();
  if (!t || t->state != State::Pending || recording_ || !allowed() || !b_.setTrim) return;
  start = std::clamp(start, 0.0, 0.98);
  end = std::clamp(end, start + 0.02, 1.0);
  b_.setTrim(t->id, start, end, answer({}));  // the new values come with takeReady
}

void StudioController::play(const QString& mode) {
  const StudioTake* t = current();
  if (!t || recording_ || !b_.play) return;
  if (t->state == State::Withdrawn) return void(say(L::Caution, tr("Consent withdrawn: this take is not played.")));
  if (!allowed()) return;
  b_.play(t->id, mode.toStdString(), answer({}));
}

void StudioController::stopPlay() {
  if (b_.stopPlay) b_.stopPlay();
}

void StudioController::accept(bool primary) {
  const StudioTake* t = current();
  if (!t || recording_ || !allowed() || !b_.accept) return;
  if (t->state == State::Withdrawn || (!t->synthetic && !mayUse(q(t->speaker))))
    return void(say(L::Caution, tr("No consent for the pilot voice: this take cannot be used.")));
  if ((primary && t->state == State::Primary) || (!primary && t->state == State::Alternate)) return;
  b_.accept(t->id, primary, answer([this] { loadClips(); }, primary ? tr("Primary take.") : tr("Alternate take.")));
}

void StudioController::reject() {
  const StudioTake* t = current();
  if (!t || recording_ || t->state == State::Withdrawn || t->state == State::Rejected || !allowed() || !b_.reject) return;
  b_.reject(t->id, answer([this] { loadClips(); }, tr("Rejected; the file is kept.")));
}

void StudioController::deleteTake() {
  const StudioTake* t = current();
  if (!t || recording_ || !(t->state == State::Withdrawn || t->state == State::Rejected) || !allowed() || !b_.deleteTake) return;
  b_.deleteTake(t->id, answer([this] { loadClips(); }, tr("Deleted.")));
}

// ---- coverage

QStringList StudioController::scenarios() const {
  QStringList l;
  for (const StudioScenario& s : content_.scenarios) l << q(s.id);
  return l;
}

QString StudioController::coverage() const {
  int needed = 0, primary = 0, miss = 0;
  for (const StudioClip& c : clips_) {
    if (!c.needed) continue;
    ++needed;
    primary += stateOf(c) == ClipState::Primary;
    miss += stateOf(c) == ClipState::Missing;
  }
  return tr("Clips %1   Primary %2   Missing %3").arg(needed).arg(primary).arg(miss);
}

QStringList StudioController::unused() const {
  QStringList l;
  for (const StudioClip& c : clips_)
    if (!c.needed) l << q(c.id);
  return l;
}

QStringList StudioController::differing() const {
  QStringList l;
  for (const StudioClip& c : clips_)
    if (c.needed && !c.bankWords.empty() && c.bankWords != c.words) l << tr("%1: \"%2\", text \"%3\"").arg(q(c.id), q(c.bankWords), q(c.words));
  return l;
}

QString StudioController::voice() const {
  if (content_.activeVoice.empty() || content_.activeVoice == content_.voice) return q(content_.voice);
  return tr("%1 (exercise: %2)").arg(q(content_.voice), q(content_.activeVoice));
}

bool StudioController::matches(const StudioClip& c) const {
  if (filter_ == "this") return std::find(c.replies.begin(), c.replies.end(), draft_.id) != c.replies.end();
  if (filter_ == "pools") return !c.pool.empty();
  if (filter_ == "all") return true;
  return q(c.position) == filter_;
}

std::vector<const StudioClip*> StudioController::missing() const {
  std::vector<const StudioClip*> l;
  for (const StudioClip& c : clips_)
    if (c.needed && stateOf(c) == ClipState::Missing && matches(c)) l.push_back(&c);
  return l;
}

void StudioController::setScenario(const QString& id) {
  if (id == scenario()) return;
  if (recording_) return void(say(L::Caution, tr("Stop the recording first.")));
  scenario_ = id.toStdString();
  emit coverageChanged();
  loadClips();
}

void StudioController::setFilter(const QString& f) {
  if (f == filter_) return;
  filter_ = f;
  emit coverageChanged();
}

void StudioController::stepMissing(int direction) {
  if (recording_) return void(say(L::Caution, tr("Stop the recording first.")));
  const auto l = missing();
  if (l.empty()) return void(say(L::Ok, tr("No clip is missing.")));
  const int n = int(l.size());
  int at = -1;
  for (int i = 0; i < n; ++i)
    if (l[std::size_t(i)]->id == clipId_) at = i;
  const int next = at < 0 ? (direction > 0 ? 0 : n - 1) : (at + (direction > 0 ? 1 : n - 1)) % n;
  const StudioClip c = *l[std::size_t(next)];
  clearMessage();
  selectClip(q(c.id));
  const bool here = std::find(c.replies.begin(), c.replies.end(), draft_.id) != c.replies.end() && draft_.reply;
  if (!here && !c.replies.empty() && !dirty()) {  // an edit is not left; the clip is shown all the same
    replies_ = true;
    for (const StudioEntry& e : content_.entries)
      if (e.reply && e.id == c.replies.front()) {
        open(e);
        break;
      }
    emit listChanged();
  } else {
    segment_ = segmentOf(clipId_);
    emit stripChanged();
  }
}

void StudioController::exportChanges(const QString& file) {
  if (!allowed() || !b_.exportChanges) return;
  if (file.trimmed().isEmpty()) return void(say(L::Caution, tr("Enter a file name.")));
  b_.exportChanges(file.trimmed().toStdString(), answer({}, tr("Exported to %1").arg(file.trimmed())));
}
