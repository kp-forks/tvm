/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file builtin_fp16.h
 * \brief Functions for conversion between fp32 and fp16
 */
#ifndef TVM_RUNTIME_BUILTIN_FP16_H_
#define TVM_RUNTIME_BUILTIN_FP16_H_

#include <tvm/runtime/base.h>

#include <cstdint>

extern "C" {
TVM_DLL uint16_t __gnu_f2h_ieee(float);
TVM_DLL float __gnu_h2f_ieee(uint16_t);
TVM_DLL uint16_t __truncsfhf2(float v);
TVM_DLL uint16_t __truncdfhf2(double v);
TVM_DLL float __extendhfsf2(uint16_t v);
}

#endif  // TVM_RUNTIME_BUILTIN_FP16_H_
