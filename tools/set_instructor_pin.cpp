// Sets or replaces the instructor PIN. Run it on the panel, in a terminal: ./set_instructor_pin
#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "core/PathResolver.h"
#include "storage/InstructorPin.h"

static std::string ask(const char* prompt) {
  std::cout << prompt << std::flush;
  termios old{};
  const bool tty = tcgetattr(STDIN_FILENO, &old) == 0;
  if (tty) {
    termios quiet = old;
    quiet.c_lflag &= ~tcflag_t(ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
  }
  std::string s;
  std::getline(std::cin, s);
  if (tty) tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
  std::cout << '\n';
  return s;
}

int main() {
  atc::Paths paths;
  std::string error;
  if (!atc::resolvePaths(atc::systemPathInputs(), paths, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 2;
  }
  const std::string pin = ask("New instructor PIN (6 to 12 digits): ");
  if (!atc::validPin(pin)) {
    std::fprintf(stderr, "The PIN must be 6 to 12 digits. Nothing changed.\n");
    return 1;
  }
  if (ask("Again: ") != pin) {
    std::fprintf(stderr, "The two entries differ. Nothing changed.\n");
    return 1;
  }
  const auto hash = atc::hashPin(pin);
  if (!hash || !atc::savePinHash(paths.writable + "/instructor.pin", *hash)) {
    std::fprintf(stderr, "The PIN could not be stored. Nothing changed.\n");
    return 1;
  }
  std::printf("Instructor PIN set.\n");
  return 0;
}
