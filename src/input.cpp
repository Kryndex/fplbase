// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "precompiled.h"
#include "fplbase/input.h"
#include "fplbase/utilities.h"
#if ANDROID_GAMEPAD
#include <jni.h>
#include <android/keycodes.h>
#include <android/input.h>
#endif  // ANDROID_GAMEPAD

//#if defined(_DEBUG) || DEBUG==1
//#define LOG_FRAMERATE
//#endif

namespace fpl {

// ANDROID_GAMEPAD is defined in input.h, if we're running on an android device.
#if ANDROID_GAMEPAD
const int kMaxAndroidEventsPerFrame = 100;
#endif  // ANDROID_GAMEPAD

// Maximum range (+/-) generated by joystick axis events
static const float kJoystickAxisRange = 32767.0;

// static member function
int InputSystem::HandleAppEvents(void *userdata, void *ev) {
  auto event = reinterpret_cast<SDL_Event *>(ev);
  auto input_system = reinterpret_cast<InputSystem *>(userdata);
  int passthrough = 0;
  switch (event->type) {
    case SDL_APP_TERMINATING:
      break;
    case SDL_APP_LOWMEMORY:
      break;
    case SDL_APP_WILLENTERBACKGROUND:
      input_system->set_minimized(true);
      input_system->set_minimized_frame(input_system->frames());
#ifdef __ANDROID__
      // Work around for an invalid window reference in mouse input.
      input_system->relative_mouse_mode_ = input_system->RelativeMouseMode();
      input_system->SetRelativeMouseMode(true);
      LogInfo("CurrentMouseMode:%d", input_system->relative_mouse_mode_);
#endif
      break;
    case SDL_APP_DIDENTERBACKGROUND:
      break;
    case SDL_APP_WILLENTERFOREGROUND:
      break;
    case SDL_APP_DIDENTERFOREGROUND:
      input_system->set_minimized(false);
      input_system->set_minimized_frame(input_system->frames());
#ifdef __ANDROID__
      // Restore relative mousemode.
      input_system->SetRelativeMouseMode(input_system->relative_mouse_mode_);
#endif
      break;
    default:
      passthrough = 1;
      break;
  }
  if (!passthrough && event->type != SDL_APP_TERMINATING) {
    for (auto callback = input_system->app_event_callbacks().begin();
         callback != input_system->app_event_callbacks().end(); ++callback) {
      (*callback)(event);
    }
  }
  return passthrough;
}

void InputSystem::Initialize() {
  // Set callback to hear about lifecycle events on mobile devices.
  SDL_SetEventFilter(reinterpret_cast<SDL_EventFilter>(HandleAppEvents), this);

  UpdateConnectedJoystickList();

  // Initialize time.
  start_time_ = SDL_GetPerformanceCounter();
  time_freq_ = SDL_GetPerformanceFrequency();
  elapsed_time_ = -0.02f;    // Ensure first frame doesn't get a crazy delta.
}

void InputSystem::AddAppEventCallback(AppEventCallback callback) {
  app_event_callbacks_.push_back(callback);
}


void InputSystem::AdvanceFrame(vec2i *window_size) {
  // Update timing.
  auto current = RealTime();
  frame_time_ = current - elapsed_time_;
  elapsed_time_ = current;
  frames_++;

#ifdef __ANDROID__
  // For performance mode, we send keypress events to the Android system, so
  // that it knows it's still in active use, even if the user isn't currently
  // touching the screen.
  const HighPerformanceParams& hp_params = GetHighPerformanceParameters();
  double current_time = Time();
  if (current_time > last_android_keypress_ + hp_params.time_between_presses) {
    last_android_keypress_ = current_time;
    SendKeypressEventToAndroid(hp_params.android_key_code);
  }
#endif

#ifdef LOG_FRAMERATE
  // Framerate statistics over the last N frames.
  const int kNumDeltaTimes = 64;
  static int cur_delta_idx = 0;
  static double delta_times[kNumDeltaTimes];
  delta_times[cur_delta_idx++] = DeltaTime();
  if (cur_delta_idx == kNumDeltaTimes) {
    cur_delta_idx = 0;
    double sum_delta = 0, min_delta = 99999, max_delta = 0;
    const int kMaxBins = 8;
    int bins[kMaxBins] = { 0 };
    for (int i = 0; i < kNumDeltaTimes; i++) {
      sum_delta += delta_times[i];
      min_delta = std::min(min_delta, delta_times[i]);
      max_delta = std::max(max_delta, delta_times[i]);
      // Frame-count "bin".
      // Bin 0: 0 ~ 16.6ms (=1/60s)
      // Bin 1: 16.6 ~ 32.3ms (=2/60s)
      // Bin 2: 32.3 ~ 50ms (=3/60s)
      auto bin = static_cast<int>(delta_times[i] * 60.0);
      bins[std::max(1, std::min(bin, kMaxBins - 1))]++;
    }
    std::ostringstream os;
    for (int i = 1; i < kMaxBins; i++) {
      os << bins[i] << ";";
    }
    LogInfo(kApplication,
            "DeltaTime (ms) avg: %.1f, max: %.1f, min: %.1f, bins: %s",
            (sum_delta / kNumDeltaTimes * 1000),
            max_delta * 1000,
            min_delta * 1000,
            os.str().c_str());
  }
#endif

  // Reset our per-frame input state.
  mousewheel_delta_ = mathfu::kZeros2i;
  for (auto it = button_map_.begin(); it != button_map_.end(); ++it) {
    it->second.AdvanceFrame();
  }
  for (auto it = pointers_.begin(); it != pointers_.end(); ++it) {
    it->mousedelta = mathfu::kZeros2i;
    if (touch_device_ && !it->used) {
      // A lifted finger should not intersect any position on screen.
      it->mousepos = vec2i(-1, -1);
    }
  }
  for (auto it = joystick_map_.begin(); it != joystick_map_.end(); ++it) {
    it->second.AdvanceFrame();
  }
#if ANDROID_GAMEPAD
  for (auto it = gamepad_map_.begin(); it != gamepad_map_.end(); ++it) {
    it->second.AdvanceFrame();
  }
  HandleGamepadEvents();
#endif  // ANDROID_GAMEPAD
  if (!record_text_input_) {
    text_input_events_.clear();
  }

  // Poll events until Q is empty.
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        exit_requested_ = true;
        break;
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        GetButton(event.key.keysym.sym).Update(event.key.state == SDL_PRESSED);
        if (record_text_input_) {
          text_input_events_.push_back(TextInputEvent(
              kTextInputEventTypeKey, event.key.state, (event.key.repeat != 0),
              event.key.keysym.sym, event.key.keysym.mod));
        }
        break;
      }
#ifdef PLATFORM_MOBILE
      case SDL_FINGERDOWN: {
        touch_device_ = true;
        int i = UpdateDragPosition(&event.tfinger, event.type, *window_size);
        GetPointerButton(i).Update(true);
        break;
      }
      case SDL_FINGERUP: {
        touch_device_ = true;
        int i = FindPointer(event.tfinger.fingerId);
        RemovePointer(i);
        GetPointerButton(i).Update(false);
        break;
      }
      case SDL_FINGERMOTION: {
        touch_device_ = true;
        UpdateDragPosition(&event.tfinger, event.type, *window_size);
        break;
      }
#else  // PLATFORM_MOBILE
      // These fire from e.g. OS X touchpads. Ignore them because we just
      // want the mouse events.
      case SDL_FINGERDOWN:
        break;
      case SDL_FINGERUP:
        break;
      case SDL_FINGERMOTION:
        break;
#endif  // PLATFORM_MOBILE
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        touch_device_ = false;
        GetPointerButton(event.button.button - 1)
            .Update(event.button.state == SDL_PRESSED);
        pointers_[0].mousepos = vec2i(event.button.x, event.button.y);
        pointers_[0].used = true;
#if ANDROID_CARDBOARD
        if (event.button.state == SDL_PRESSED) {
          cardboard_input_.OnCardboardTrigger();
        }
#endif  // ANDROID_CARDBOARD
        break;
      }
      case SDL_MOUSEMOTION: {
        // Mouse events are superfluous on mobile platforms as they're simply
        // a backward compatible way of sending finger up / down / motion
        // events.
#if !defined(PLATFORM_MOBILE)
        touch_device_ = false;
        pointers_[0].mousedelta += vec2i(event.motion.xrel, event.motion.yrel);
        pointers_[0].mousepos = vec2i(event.button.x, event.button.y);
#endif  // !defined(PLATFORM_MOBILE)
        break;
      }
      case SDL_MOUSEWHEEL: {
        touch_device_ = false;
        mousewheel_delta_ += vec2i(event.wheel.x, event.wheel.y);
        break;
      }
      case SDL_WINDOWEVENT: {
        switch (event.window.event) {
          case SDL_WINDOWEVENT_RESIZED:
            *window_size = vec2i(event.window.data1, event.window.data2);
            break;
        }
        break;
      }
      case SDL_JOYAXISMOTION:
      case SDL_JOYBUTTONDOWN:
      case SDL_JOYBUTTONUP:
      case SDL_JOYHATMOTION:
      case SDL_JOYDEVICEADDED:
      case SDL_JOYDEVICEREMOVED: {
        HandleJoystickEvent(&event);
        break;
      }
      case SDL_TEXTEDITING:
        if (record_text_input_) {
          TextInputEvent edit(kTextInputEventTypeEdit);
          text_input_events_.push_back(TextInputEvent(kTextInputEventTypeEdit,
                                          event.edit.text, event.edit.start,
                                          event.edit.length));
        }
        break;
      case SDL_TEXTINPUT: {
        if (record_text_input_) {
          text_input_events_.push_back(TextInputEvent(kTextInputEventTypeText,
                                          event.text.text));
        }
        break;
      }
      case SDL_MULTIGESTURE: {
        // We don't do anything with gesture events at the moment.
        break;
      }
      default: {
        LogInfo(kApplication, "----Unknown SDL event!\n");
        LogInfo(kApplication, "----Event ID: 0x%x!\n", event.type);
      }
    }
  }
  // Update the Cardboard input. Note this is after the mouse input, as that can
  // be treated as a trigger.
#if ANDROID_CARDBOARD
  cardboard_input_.AdvanceFrame();
#endif  // ANDROID_CARDBOARD
}

void InputSystem::HandleJoystickEvent(Event event) {
  SDL_Event *sdl_event = static_cast<SDL_Event *>(event);
  switch (sdl_event->type) {
    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED:
      UpdateConnectedJoystickList();
      break;
    case SDL_JOYAXISMOTION:
      // Axis data is normalized to a range of [-1.0, 1.0]
      GetJoystick(sdl_event->jaxis.which)
          .SetAxis(sdl_event->jaxis.axis,
                   sdl_event->jaxis.value / kJoystickAxisRange);
      break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
      GetJoystick(sdl_event->jbutton.which)
          .GetButton(sdl_event->jbutton.button)
          .Update(sdl_event->jbutton.state == SDL_PRESSED);
      break;
    case SDL_JOYHATMOTION:
      GetJoystick(sdl_event->jhat.which)
          .SetHat(sdl_event->jhat.hat,
                  ConvertHatToVector(sdl_event->jhat.value));
      break;
  }
}

// Convert SDL joystick hat enum values into more generic 2d vectors.
vec2 InputSystem::ConvertHatToVector(uint32_t hat_enum) const {
  switch (hat_enum) {
    case SDL_HAT_LEFTUP:
      return vec2(-1, -1);
    case SDL_HAT_UP:
      return vec2(0, -1);
    case SDL_HAT_RIGHTUP:
      return vec2(1, -1);
    case SDL_HAT_LEFT:
      return vec2(-1, 0);
    case SDL_HAT_CENTERED:
      return vec2(0, 0);
    case SDL_HAT_RIGHT:
      return vec2(1, 0);
    case SDL_HAT_LEFTDOWN:
      return vec2(-1, 1);
    case SDL_HAT_DOWN:
      return vec2(0, 1);
    case SDL_HAT_RIGHTDOWN:
      return vec2(1, 1);
    default:
      LogError(
          kApplication,
          "InputSystem::ConvertHatToVector: Unknown SDL Hat Enum Value!\n");
      return vec2(0, 0);
  }
}

double InputSystem::Time() const {
  return elapsed_time_;
}

double InputSystem::RealTime() const {
  assert(time_freq_);
  return static_cast<double>(SDL_GetPerformanceCounter() - start_time_)
          / static_cast<double>(time_freq_);
}

double InputSystem::DeltaTime() const {
  return frame_time_;
}

Button &InputSystem::GetButton(int button) {
  auto it = button_map_.find(button);
  return it != button_map_.end() ? it->second
                                 : (button_map_[button] = Button());
}

bool InputSystem::RelativeMouseMode() const {
  return SDL_GetRelativeMouseMode() == SDL_TRUE;
}

void InputSystem::SetRelativeMouseMode(bool enabled) {
#if defined(__ANDROID__)
  (void)enabled;
#else
  // SDL on Android does not support relative mouse mode.  Enabling this
  // causes a slew of errors reported caused by the SDL_androidtouch.c module
  // sending touch events to SDL_SendMouseMotion() without a window handle,
  // where the window handle is required to get the screen size in order
  // to move the mouse pointer (not present on Android) back to the middle of
  // the screen.
  SDL_SetRelativeMouseMode(static_cast<SDL_bool>(enabled));
#endif  // defined(__ANDROID__)
}

Joystick &InputSystem::GetJoystick(JoystickId joystick_id) {
  auto it = joystick_map_.find(joystick_id);
  assert(it != joystick_map_.end());
  return it->second;
}
#if ANDROID_GAMEPAD
Gamepad &InputSystem::GetGamepad(AndroidInputDeviceId gamepad_device_id) {
  auto it = gamepad_map_.find(gamepad_device_id);
  if (it == gamepad_map_.end()) {
    Gamepad &gamepad = gamepad_map_[gamepad_device_id];
    gamepad.set_controller_id(gamepad_device_id);
    return gamepad;
  } else {
    return it->second;
  }
}
#endif  // ANDROID_GAMEPAD

void InputSystem::RemovePointer(size_t i) {
  // Can't clear the rest of the state here yet, since it may still
  // need to be queried this frame.
  pointers_[i].used = false;
}

size_t InputSystem::FindPointer(FingerId id) {
  for (size_t i = 0; i < pointers_.size(); i++) {
    if (pointers_[i].used && pointers_[i].id == id) {
      return i;
    }
  }
  for (size_t i = 0; i < pointers_.size(); i++) {
    if (!pointers_[i].used) {
      pointers_[i].id = id;
      pointers_[i].used = true;
      return i;
    }
  }
  assert(0);
  return 0;
}

size_t InputSystem::UpdateDragPosition(TouchFingerEvent event,
                                       uint32_t event_type,
                                       const vec2i &window_size) {
  // This is a bit clumsy as SDL has a list of pointers and so do we,
  // but they work a bit differently: ours is such that the first one is
  // always the first one that went down, making it easier to write code
  // that works well for both mouse and touch.
  SDL_TouchFingerEvent *e = static_cast<SDL_TouchFingerEvent *>(event);
  int numfingers = SDL_GetNumTouchFingers(e->touchId);
  for (int i = 0; i < numfingers; i++) {
    auto finger = SDL_GetTouchFinger(e->touchId, i);
    if (finger->id == e->fingerId) {
      auto j = FindPointer(e->fingerId);
      if (event_type == SDL_FINGERUP) RemovePointer(j);
      auto &p = pointers_[j];
      auto event_position = vec2(e->x, e->y);
      auto event_delta = vec2(e->dx, e->dy);
      p.mousepos = vec2i(event_position * vec2(window_size));
      p.mousedelta += vec2i(event_delta * vec2(window_size));
      return j;
    }
  }
  return 0;
}

void InputSystem::UpdateConnectedJoystickList() {
  CloseOpenJoysticks();
  OpenConnectedJoysticks();
}

void InputSystem::OpenConnectedJoysticks() {
  // Make sure we're set up to receive events from these.
  SDL_InitSubSystem(SDL_INIT_JOYSTICK);
  SDL_JoystickEventState(SDL_ENABLE);

  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    // Tell SDL that we're interested in getting updates for this joystick.
    SDL_Joystick *sdl_joystick = SDL_JoystickOpen(i);

    // Create our Joystick structure, if it doesn't already exist for this
    // joystick_id. Note that our Joystick structure is never removed from
    // the map.
    JoystickId joystick_id = SDL_JoystickInstanceID(sdl_joystick);
    auto it = joystick_map_.find(joystick_id);
    if (it == joystick_map_.end()) {
      joystick_map_[joystick_id] = Joystick();
    }

    // Remember the SDL handle for this joystick.
    joystick_map_[joystick_id].set_joystick_data(sdl_joystick);
  }
}

void InputSystem::CloseOpenJoysticks() {
  for (auto it = joystick_map_.begin(); it != joystick_map_.end(); ++it) {
    Joystick &joystick = it->second;
    SDL_JoystickClose(static_cast<SDL_Joystick *>(joystick.joystick_data()));
    joystick.set_joystick_data(nullptr);
  }
}

const std::vector<TextInputEvent> *InputSystem::GetTextInputEvents() {
  return &text_input_events_;
}

void InputSystem::StartTextInput() { SDL_StartTextInput(); }

void InputSystem::StopTextInput() { SDL_StopTextInput(); }

void InputSystem::SetTextInputRect(const mathfu::vec4 &input_rect) {
  SDL_Rect rect;
  rect.x = static_cast<size_t>(input_rect.x());
  rect.y = static_cast<size_t>(input_rect.y());
  rect.w = static_cast<size_t>(input_rect.z());
  rect.h = static_cast<size_t>(input_rect.w());
  SDL_SetTextInputRect(&rect);
}

void Button::Update(bool down) {
  if (!is_down_ && down) {
    went_down_ = true;
  } else if (is_down_ && !down) {
    went_up_ = true;
  }
  is_down_ = down;
}

Button &Joystick::GetButton(size_t button_index) {
  if (button_index >= button_list_.size()) {
    button_list_.resize(button_index + 1);
  }
  return button_list_[button_index];
}

float Joystick::GetAxis(size_t axis_index) {
  while (axis_index >= axis_list_.size()) {
    axis_list_.push_back(0);
  }
  return axis_list_[axis_index];
}

vec2 Joystick::GetHat(size_t hat_index) {
  while (hat_index >= hat_list_.size()) {
    hat_list_.push_back(mathfu::kZeros2f);
  }
  return hat_list_[hat_index];
}

void Joystick::SetAxis(size_t axis_index, float axis) {
  GetAxis(axis_index);
  axis_list_[axis_index] = axis;
}

void Joystick::SetHat(size_t hat_index, const vec2 &hat) {
  GetHat(hat_index);
  hat_list_[hat_index] = hat;
}

// Reset the per-frame input on all our sub-elements
void Joystick::AdvanceFrame() {
  for (size_t i = 0; i < button_list_.size(); i++) {
    button_list_[i].AdvanceFrame();
  }
}

JoystickId Joystick::GetJoystickId() const {
  return SDL_JoystickInstanceID(static_cast<SDL_Joystick *>(joystick_data_));
}

int Joystick::GetNumButtons() const {
  return SDL_JoystickNumButtons(static_cast<SDL_Joystick *>(joystick_data_));
}

int Joystick::GetNumAxes() const {
  return SDL_JoystickNumAxes(static_cast<SDL_Joystick *>(joystick_data_));
}

int Joystick::GetNumHats() const {
  return SDL_JoystickNumHats(static_cast<SDL_Joystick *>(joystick_data_));
}

#if ANDROID_GAMEPAD
std::queue<AndroidInputEvent> InputSystem::unhandled_java_input_events_;

void InputSystem::ReceiveGamepadEvent(AndroidInputDeviceId device_id,
                                      int event_code, int control_code, float x,
                                      float y) {
  if (unhandled_java_input_events_.size() < kMaxAndroidEventsPerFrame) {
    pthread_mutex_lock(&android_event_mutex);
    unhandled_java_input_events_.push(
        AndroidInputEvent(device_id, event_code, control_code, x, y));
    pthread_mutex_unlock(&android_event_mutex);
  }
}

// Process and handle the events we have received from Java.
void InputSystem::HandleGamepadEvents() {
  AndroidInputEvent event;
  pthread_mutex_lock(&android_event_mutex);
  while (unhandled_java_input_events_.size() > 0) {
    event = unhandled_java_input_events_.front();
    unhandled_java_input_events_.pop();

    Gamepad &gamepad = GetGamepad(event.device_id);
    Gamepad::GamepadInputButton button_index;

    switch (event.event_code) {
      case AKEY_EVENT_ACTION_DOWN:
        button_index = static_cast<Gamepad::GamepadInputButton>(
            Gamepad::GetGamepadCodeFromJavaKeyCode(event.control_code));
        if (button_index != Gamepad::kInvalid) {
          gamepad.GetButton(button_index).Update(true);
        }
        break;
      case AKEY_EVENT_ACTION_UP:
        button_index = static_cast<Gamepad::GamepadInputButton>(
            Gamepad::GetGamepadCodeFromJavaKeyCode(event.control_code));
        if (button_index != Gamepad::kInvalid) {
          gamepad.GetButton(button_index).Update(false);
        }
        break;
      case AMOTION_EVENT_ACTION_MOVE:
        const bool left = event.x < -kGamepadHatThreshold;
        const bool right = event.x > kGamepadHatThreshold;
        const bool up = event.y < -kGamepadHatThreshold;
        const bool down = event.y > kGamepadHatThreshold;

        gamepad.GetButton(Gamepad::kLeft).Update(left);
        gamepad.GetButton(Gamepad::kRight).Update(right);
        gamepad.GetButton(Gamepad::kUp).Update(up);
        gamepad.GetButton(Gamepad::kDown).Update(down);
        break;
    }
  }
  // Clear the queue
  std::queue<AndroidInputEvent>().swap(unhandled_java_input_events_);
  pthread_mutex_unlock(&android_event_mutex);
}

// Reset the per-frame input on all our sub-elements
void Gamepad::AdvanceFrame() {
  for (size_t i = 0; i < button_list_.size(); i++) {
    button_list_[i].AdvanceFrame();
  }
}

Button &Gamepad::GetButton(GamepadInputButton index) {
  assert(index >= 0 && index < Gamepad::kControlCount &&
         "Gamepad Button Index out of range");
  return button_list_[index];
}

struct JavaToGamepadMapping {
  int java_keycode;
  int gamepad_code;
};

pthread_mutex_t InputSystem::android_event_mutex = PTHREAD_MUTEX_INITIALIZER;

int Gamepad::GetGamepadCodeFromJavaKeyCode(int java_keycode) {
  // Note that DpadCenter maps onto ButtonA.  They have the same functional
  // purpose, and anyone dealing with a gamepad isn't going to want to deal with
  // the distinction.  Also, buttons 1,2,3 map onto buttons A,B,C, for basically
  // the same reason.
  static const JavaToGamepadMapping kJavaToGamepadMap[]{
      {AKEYCODE_DPAD_UP, Gamepad::kUp},
      {AKEYCODE_DPAD_DOWN, Gamepad::kDown},
      {AKEYCODE_DPAD_LEFT, Gamepad::kLeft},
      {AKEYCODE_DPAD_RIGHT, Gamepad::kRight},
      {AKEYCODE_DPAD_CENTER, Gamepad::kButtonA},
      {AKEYCODE_BUTTON_A, Gamepad::kButtonA},
      {AKEYCODE_BUTTON_B, Gamepad::kButtonB},
      {AKEYCODE_BUTTON_C, Gamepad::kButtonC}};
  for (int i = 0; i < Gamepad::kControlCount; i++) {
    if (kJavaToGamepadMap[i].java_keycode == java_keycode) {
      return kJavaToGamepadMap[i].gamepad_code;
    }
  }
  return Gamepad::kInvalid;
}

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_com_google_fpl_fplbase_FPLActivity_nativeOnGamepadInput(
    JNIEnv *env, jobject thiz, jint controller_id, jint event_code,
    jint control_code, jfloat x, jfloat y) {
  InputSystem::ReceiveGamepadEvent(
      static_cast<AndroidInputDeviceId>(controller_id), event_code,
      control_code, x, y);
}
#endif  //__ANDROID__
#endif  // ANDROID_GAMEPAD

#if ANDROID_CARDBOARD
CardboardInput InputSystem::cardboard_input_;

void CardboardInput::AdvanceFrame() {
  UpdateCardboardTransforms();

  triggered_ = pending_trigger_;
  pending_trigger_ = false;
}

void CardboardInput::ResetHeadTracker() {
#ifdef __ANDROID__
  JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
  jobject activity = reinterpret_cast<jobject>(SDL_AndroidGetActivity());
  jclass fpl_class = env->GetObjectClass(activity);
  jmethodID reset_head_tracker =
      env->GetMethodID(fpl_class, "ResetHeadTracker", "()V");
  env->CallVoidMethod(activity, reset_head_tracker);
  env->DeleteLocalRef(fpl_class);
  env->DeleteLocalRef(activity);
#endif  // __ANDROID__
}

void CardboardInput::UpdateCardboardTransforms() {
#ifdef __ANDROID__
  JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
  jobject activity = reinterpret_cast<jobject>(SDL_AndroidGetActivity());
  jclass fpl_class = env->GetObjectClass(activity);
  jmethodID get_eye_views =
      env->GetMethodID(fpl_class, "GetEyeViews", "([F[F)V");
  jfloatArray left_eye = env->NewFloatArray(16);
  jfloatArray right_eye = env->NewFloatArray(16);
  env->CallVoidMethod(activity, get_eye_views, left_eye, right_eye);
  jfloat *left_eye_floats = env->GetFloatArrayElements(left_eye, NULL);
  jfloat *right_eye_floats = env->GetFloatArrayElements(right_eye, NULL);
  left_eye_transform_ = mat4(left_eye_floats);
  right_eye_transform_ = mat4(right_eye_floats);
  if (use_device_orientation_correction_) {
    mat4 post_correction = mat4::Identity();
    mat4 pre_correction = mat4::Identity();
    switch (device_orientation_) {
      case 0: {
        // 0 degree rotation.
        pre_correction =
            mat4::FromRotationMatrix(mathfu::mat3::RotationY(M_PI_2));
        post_correction =
            mat4::FromRotationMatrix(mathfu::mat3::RotationZ(-M_PI_2));
        break;
      }
      case 2: {
        // 180 degree rotation.
        pre_correction =
            mat4::FromRotationMatrix(mathfu::mat3::RotationY(-M_PI_2));
        post_correction =
            mat4::FromRotationMatrix(mathfu::mat3::RotationZ(M_PI_2));
        break;
      }
      case 3: {
        // 270 degree rotation.
        pre_correction =
            mat4::FromRotationMatrix(mathfu::mat3::RotationY(-M_PI));
        post_correction =
            mat4::FromRotationMatrix(mathfu::mat3::RotationZ(M_PI));
        break;
      }
      default: {
        // The HMD SDK assumes a 90 degree rotation, so nothing to do.
        break;
      }
    }
    left_eye_transform_ =
        post_correction * left_eye_transform_ * pre_correction;
    right_eye_transform_ =
        post_correction * right_eye_transform_ * pre_correction;
  }
  env->ReleaseFloatArrayElements(left_eye, left_eye_floats, JNI_ABORT);
  env->ReleaseFloatArrayElements(right_eye, right_eye_floats, JNI_ABORT);
  env->DeleteLocalRef(left_eye);
  env->DeleteLocalRef(right_eye);
  env->DeleteLocalRef(fpl_class);
  env->DeleteLocalRef(activity);
#endif  // __ANDROID__
}

void CardboardInput::EnableDeviceOrientationCorrection() {
  use_device_orientation_correction_ = true;
#ifdef __ANDROID__
  JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
  jobject activity = reinterpret_cast<jobject>(SDL_AndroidGetActivity());
  jclass fpl_class = env->GetObjectClass(activity);
  jmethodID get_display_rotation =
      env->GetMethodID(fpl_class, "GetDisplayRotation", "()I");
  device_orientation_ = env->CallIntMethod(activity, get_display_rotation);
  env->DeleteLocalRef(fpl_class);
  env->DeleteLocalRef(activity);
#endif  // __ANDROID__
}

void InputSystem::OnCardboardTrigger() {
  cardboard_input_.OnCardboardTrigger();
}

void InputSystem::SetDeviceInCardboard(bool in_cardboard) {
  cardboard_input_.set_is_in_cardboard(in_cardboard);
}
#endif  // ANDROID_CARDBOARD

// Constructors and destroctor of TextInputEvent union.
TextInputEvent::TextInputEvent(TextInputEventType t) {
  type = t;
  switch (type) {
    case kTextInputEventTypeEdit:
    case kTextInputEventTypeText:
      text = std::string();
      break;
    case kTextInputEventTypeKey:
      break;
  }
}

TextInputEvent::TextInputEvent(TextInputEventType t, int32_t state, bool repeat,
                               int32_t symbol, int32_t modifier) {
  type = t;
  switch (type) {
    case kTextInputEventTypeKey:
      key.state = (state != 0);
      key.repeat = repeat;
      key.symbol = symbol;
      key.modifier = static_cast<FPL_Keymod>(modifier);
      break;
    case kTextInputEventTypeEdit:
    case kTextInputEventTypeText:
      assert(0);
      break;
  }
}

TextInputEvent::TextInputEvent(TextInputEventType t, const char * str) {
  type = t;
  switch (type) {
    case kTextInputEventTypeText:
      text = std::string(str);
      break;
    case kTextInputEventTypeEdit:
    case kTextInputEventTypeKey:
      assert(0);
      break;
  }
}

TextInputEvent::TextInputEvent(TextInputEventType t, const char * str,
                               int32_t start, int32_t length) {
  type = t;
  switch (type) {
    case kTextInputEventTypeEdit:
      text = std::string(str);
      edit.start = start;
      edit.length = length;
      break;
    case kTextInputEventTypeText:
    case kTextInputEventTypeKey:
      assert(0);
      break;
  }
}

// Because these calls are present in the Activity, they should be present for
// Android, even without the Cardboard flag
#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_com_google_fpl_fplbase_FPLActivity_nativeOnCardboardTrigger(
    JNIEnv *env) {
#if ANDROID_CARDBOARD
  InputSystem::OnCardboardTrigger();
#endif  // ANDROID_CARDBOARD
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_fpl_fplbase_FPLActivity_nativeSetDeviceInCardboard(
    JNIEnv *env, jobject thiz, jboolean in_cardboard) {
#if ANDROID_CARDBOARD
  InputSystem::SetDeviceInCardboard(in_cardboard);
#endif
}
#endif  // __ANDROID__

}  // namespace fpl
