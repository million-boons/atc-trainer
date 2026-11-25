#pragma once
#include <QString>
#include <QVariantList>

#include "app/ExerciseController.h"
#include "app/PlannerBackend.h"

// The cases of docs/mockups/exercise.html. Every value is simulated and all wording is PLACEHOLDER.
namespace exercise_mock {
bool isCase(const QString& id);       // id starts with "ex"
bool running(const QString& id);      // an exercise is running in this case
QString radioCase(const QString& id); // MockBackend case for the radio
QVariantList cases();                 // {id, name}
void show(ExerciseController& c, const QString& id, bool assessed, bool consent, const PlanChoice* plan = nullptr);  // plan: for the Start dialog
}  // namespace exercise_mock
