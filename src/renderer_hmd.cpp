// Copyright 2015 Google Inc. All rights reserved.
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

#include <EGL/egl.h>

#include "precompiled.h"
#include "fplbase/renderer_hmd.h"

namespace fpl {

// The framebuffer that is used for undistortion in Head Mounted Displays.
// After rendering to it, passed to Cardboard's undistortTexture call, which
// will transform and render it appropriately.
GLuint g_undistort_framebuffer_id = 0;
// The texture, needed for the color
GLuint g_undistort_texture_id = 0;
// The renderbuffer, needed for the depth
GLuint g_undistort_renderbuffer_id = 0;

void InitializeUndistortFramebuffer(int width, int height) {
  // Set up a framebuffer that matches the window, such that we can render to
  // it, and then undistort the result properly for HMDs.
  GL_CALL(glGenTextures(1, &g_undistort_texture_id));
  GL_CALL(glBindTexture(GL_TEXTURE_2D, g_undistort_texture_id));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
  GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
                       GL_UNSIGNED_BYTE, nullptr));

  GL_CALL(glGenRenderbuffers(1, &g_undistort_renderbuffer_id));
  GL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, g_undistort_renderbuffer_id));
  GL_CALL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width,
                                height));

  GL_CALL(glGenFramebuffers(1, &g_undistort_framebuffer_id));
  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, g_undistort_framebuffer_id));

  GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, g_undistort_texture_id, 0));
  GL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                    GL_RENDERBUFFER,
                                    g_undistort_renderbuffer_id));

  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void BeginUndistortFramebuffer() {
  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, g_undistort_framebuffer_id));
}

void FinishUndistortFramebuffer() {
  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
  JNIEnv* env = reinterpret_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
  jobject activity = reinterpret_cast<jobject>(SDL_AndroidGetActivity());
  jclass fpl_class = env->GetObjectClass(activity);
  jmethodID undistort = env->GetMethodID(fpl_class, "UndistortTexture", "(I)V");
  env->CallVoidMethod(activity, undistort, (jint)g_undistort_texture_id);
  env->DeleteLocalRef(fpl_class);
  env->DeleteLocalRef(activity);
}
}