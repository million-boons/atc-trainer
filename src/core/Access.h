#pragma once
#include <string_view>

#include "core/Time.h"

namespace atc {

// Local login (D-111). Nobody is logged in at start; there is no network and no default PIN.
enum class Role { None, Trainee, Instructor };

constexpr bool mayManagePeople(Role r) { return r == Role::Instructor; }      // D-109
constexpr bool mayDeleteRecordings(Role r) { return r == Role::Instructor; }  // D-102

constexpr std::string_view roleName(Role r) {
  return r == Role::Instructor ? "instructor" : r == Role::Trainee ? "trainee" : "none";
}

// Slows down PIN guessing. Not saved: a restart clears it.
class LoginGuard {
 public:
  explicit LoginGuard(int tries = 5, Ns wait = ms(60'000)) : tries_(tries), wait_(wait) {}
  Ns left(Ns now) const { return now < until_ ? until_ - now : 0; }
  bool allowed(Ns now) const { return left(now) == 0; }
  void failed(Ns now) {
    if (++fails_ < tries_) return;
    fails_ = 0;
    until_ = now + wait_;
  }
  void succeeded() { fails_ = 0; }

 private:
  const int tries_;
  const Ns wait_;
  int fails_ = 0;
  Ns until_ = 0;
};

// Is a transmission saved? (D-101, D-108, D-112.) Never affects PTT.
enum class SaveRule { Save, Off, NotLoggedIn, NoConsent };
constexpr SaveRule saveRule(bool switchedOn, Role role, bool consent) {
  return !switchedOn ? SaveRule::Off : role != Role::Trainee ? SaveRule::NotLoggedIn : !consent ? SaveRule::NoConsent : SaveRule::Save;
}

}  // namespace atc
