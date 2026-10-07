// Japanese names of the gestures and board actions, as shown on the screens.
#pragma once

#include "heatboard/gesture_control.h"

namespace gesture_names {

inline const char* gesture(heatboard::Gesture gesture) {
  switch (gesture) {
    case heatboard::Gesture::None: return "なし";
    case heatboard::Gesture::One: return "指1本";
    case heatboard::Gesture::Two: return "指2本（ピース）";
    case heatboard::Gesture::Three: return "指3本";
    case heatboard::Gesture::Four: return "指4本";
    case heatboard::Gesture::Five: return "パー（指5本）";
    case heatboard::Gesture::Like: return "グッド（親指上）";
    case heatboard::Gesture::Ok: return "OKサイン";
    case heatboard::Gesture::Call: return "電話（親指と小指）";
    case heatboard::Gesture::Dislike: return "親指下";
  }
  return "?";
}

inline const char* action(heatboard::BoardAction action) {
  switch (action) {
    case heatboard::BoardAction::PreviousHeat: return "前のヒート";
    case heatboard::BoardAction::NextHeat: return "次のヒート";
    case heatboard::BoardAction::StartStop: return "スタート／ストップ";
  }
  return "?";
}

}  // namespace gesture_names
