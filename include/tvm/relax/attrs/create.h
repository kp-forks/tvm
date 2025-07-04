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
 * \file tvm/relax/attrs/create.h
 * \brief Attributes for tensor creation operators.
 */
#ifndef TVM_RELAX_ATTRS_CREATE_H_
#define TVM_RELAX_ATTRS_CREATE_H_

#include <tvm/relax/expr.h>

namespace tvm {
namespace relax {

/*! \brief Attributes used in full/full_like, ones/ones_like, and zeros/zeros_like operators */
struct InitAttrs : public AttrsNodeReflAdapter<InitAttrs> {
  DataType dtype;

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<InitAttrs>().def_ro("dtype", &InitAttrs::dtype,
                                        "The data type of the created tensor.");
  }

  static constexpr const char* _type_key = "relax.attrs.InitAttrs";
  TVM_FFI_DECLARE_FINAL_OBJECT_INFO(InitAttrs, BaseAttrsNode);
};  // struct InitAttrs

/*! \brief Attributes used in tril and triu operator */
struct TriluAttrs : public AttrsNodeReflAdapter<TriluAttrs> {
  int k;

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<TriluAttrs>().def_ro(
        "k", &TriluAttrs::k,
        "The number of diagonals above or below the main diagonal to exclude or include.");
  }

  static constexpr const char* _type_key = "relax.attrs.TriluAttrs";
  TVM_FFI_DECLARE_FINAL_OBJECT_INFO(TriluAttrs, BaseAttrsNode);
};  // struct TriluAttrs

}  // namespace relax
}  // namespace tvm

#endif  // TVM_RELAX_ATTRS_CREATE_H_
