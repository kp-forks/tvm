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
 * \file tvm/ir/transform.h
 *
 * This file implements a pass manager. The pass manager manages a sequence
 * of IRModule -> IRModule transformation passes over a particlar unit of AST. The
 * design is largely inspired from LLVM's pass manager and modern deep learning
 * frameworks that perform tensor->tensor transformations.
 *
 * The responsibilities of a traditional compiler pass manager usually involves:
 *  - Organizing the execution order of optimization passes though not
 * necessarily in the optimal sequence.
 *  - Collecting required analysis information and keep them up-to-date.
 *  - Reducing the effort required to implement new passes for compiler
 * developers, etc.
 *
 * Similar to LLVM's pass manager, we designed the Relax pass manager to work
 * different granularity, i.e. module level, function level, and even sequential
 * passe that contains a host of passes.
 *
 * However, we also extend the functionality of the traditional pass manager
 * with the consideration of requirements/convention from deep learning
 * frameworks, such as Pytorch and Gluon, etc. Each pass in the Relax pass
 * manager performs the IRModule -> IRModule transformation. All
 * different types of passes, including the sequential-level pass object, are
 * essentially pass objects. This design, therefore, effectively provides users
 * a consistent and convenient interface, i.e. Pass, to play with. It offers a
 * means to ease the development and testing of Relax passes. For example, with
 * the pass manager, external users will be able to have custom passes correctly
 * scheduled without having to modify a single handcrafted pass order.
 *
 * In the future we need to describe constraints between passes. For example,
 * we may want to preserve dependencies between different passes and validate
 * them on the completion of a certain pass.
 *
 * We also need to store side information and import the error reporting system.
 */
#ifndef TVM_IR_TRANSFORM_H_
#define TVM_IR_TRANSFORM_H_

#include <tvm/ffi/container/array.h>
#include <tvm/ffi/reflection/registry.h>
#include <tvm/ffi/string.h>
#include <tvm/ir/diagnostic.h>
#include <tvm/ir/instrument.h>
#include <tvm/ir/module.h>
#include <tvm/support/with.h>

#include <string>
#include <utility>

namespace tvm {
namespace transform {

/*!
 * \brief PassContextNode contains the information that a pass can rely on,
 * such as analysis results.
 * \sa PassContext
 */
class PassContextNode : public Object {
 public:
  /*! \brief The default optimization level. */
  int opt_level{2};

  /*! \brief The list of required passes. */
  Array<String> required_pass;
  /*! \brief The list of disabled passes. */
  Array<String> disabled_pass;
  /*! \brief The diagnostic context. */
  mutable Optional<DiagnosticContext> diag_ctx;
  /*! \brief Pass specific configurations. */
  Map<String, Any> config;

  /*! \brief A list of pass instrument implementations. */
  Array<instrument::PassInstrument> instruments;

  PassContextNode() = default;

  /*!
   * \brief Get a config value from the pass context.
   *
   * \param key The config key.
   * \param default_value The default value if the key does not exist, defaults to nullptr.
   *
   * \return The result
   *
   * \tparam TOBjectRef the expected object type.
   * \throw Error if the key exists but the value does not match TObjectRef.
   */
  template <typename TObjectRef>
  Optional<TObjectRef> GetConfig(
      const std::string& key,
      Optional<TObjectRef> default_value = Optional<TObjectRef>(std::nullopt)) const {
    if (!config.defined()) return default_value;
    auto it = config.find(key);
    if (it != config.end()) {
      return Downcast<Optional<TObjectRef>>((*it).second);
    } else {
      return default_value;
    }
  }
  // variant that uses TObjectRef to enable implicit conversion to default value.
  template <typename TObjectRef>
  Optional<TObjectRef> GetConfig(const std::string& key, TObjectRef default_value) const {
    return GetConfig<TObjectRef>(key, Optional<TObjectRef>(default_value));
  }

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<PassContextNode>()
        .def_ro("opt_level", &PassContextNode::opt_level)
        .def_ro("required_pass", &PassContextNode::required_pass)
        .def_ro("disabled_pass", &PassContextNode::disabled_pass)
        .def_ro("instruments", &PassContextNode::instruments)
        .def_ro("config", &PassContextNode::config)
        .def_ro("diag_ctx", &PassContextNode::diag_ctx);
  }

  static constexpr const char* _type_key = "transform.PassContext";

  TVM_DECLARE_FINAL_OBJECT_INFO(PassContextNode, Object);
};

/*!
 * \brief PassContext that is used to configure the pass behavior.
 *
 * \code
 *
 *  auto new_ctx = PassContext::Create();
 *  ctx->opt_level = 2;
 *  With<PassContext> scope(ctx);
 *  // pass context in effect.
 *
 * \endcode
 * \sa PassContextNode
 */
class PassContext : public ObjectRef {
 public:
  PassContext() {}
  explicit PassContext(ObjectPtr<Object> n) : ObjectRef(n) {}
  /*!
   * \brief const accessor.
   * \return const access pointer.
   */
  const PassContextNode* operator->() const {
    ICHECK(get() != nullptr);
    return static_cast<const PassContextNode*>(get());
  }
  /*!
   * \brief mutable accessor.
   * \return mutable access pointer.
   */
  PassContextNode* operator->() {
    ICHECK(get() != nullptr);
    return static_cast<PassContextNode*>(get_mutable());
  }

  /*!
   * \brief Construct a PassContext containing the default configurations.
   * \return The new PassContext.
   */
  TVM_DLL static PassContext Create();
  /*!
   * \brief Get the default pass context in the current scope.
   * \return The pass context.
   */
  TVM_DLL static PassContext Current();

  /*!
   * \brief Get all supported configuration names and metadata, registered within the PassContext.
   * \return Map indexed by the config name, pointing to the metadata map as key-value
   */
  TVM_DLL static Map<String, Map<String, String>> ListConfigs();

  /*!
   * \brief Call instrument implementations' callbacks when entering PassContext.
   *        The callbacks are called in order, and if one raises an exception, the rest will not be
   *        called.
   */
  TVM_DLL void InstrumentEnterPassContext();

  /*!
   * \brief Call instrument implementations' callbacks when exiting PassContext.
   *        The callbacks are called in order, and if one raises an exception, the rest will not be
   *        called.
   */
  TVM_DLL void InstrumentExitPassContext();

  /*!
   * \brief Call instrument implementations' callbacks before a pass run.
   *        The callbacks are called in order, and if one raises an exception, the rest will not be
   *        called.
   *
   * \param mod The module that an optimization pass runs on.
   * \param info The pass information.
   *
   * \return false: the pass is skipped; true: the pass runs.
   */
  TVM_DLL bool InstrumentBeforePass(const IRModule& mod, const PassInfo& info) const;

  /*!
   * \brief Call instrument implementations callbacks after a pass run.
   *        The callbacks are called in order, and if one raises an exception, the rest will not be
   *        called.
   *
   * \param mod The module that an optimization pass runs on.
   * \param info The pass information.
   */
  TVM_DLL void InstrumentAfterPass(const IRModule& mod, const PassInfo& info) const;

  /*!
   * \brief Check whether a pass is enabled.
   * \param info The pass information.
   * \return true if the pass is enabled. Otherwise, false.
   */
  TVM_DLL bool PassEnabled(const PassInfo& info) const;

  /*!
   * \brief Register a valid configuration option and its ValueType for validation.
   *
   * \param key The configuration key.
   * \tparam ValueType The value type to be registered
   */
  template <typename ValueType>
  static int32_t RegisterConfigOption(const char* key) {
    // NOTE: we could further update the function later.
    if constexpr (std::is_base_of_v<ObjectRef, ValueType>) {
      int32_t tindex = ffi::TypeToRuntimeTypeIndex<ValueType>::v();
      auto* reflection = ReflectionVTable::Global();
      auto type_key = ffi::TypeIndexToTypeKey(tindex);
      auto legalization = [=](ffi::Any value) -> ffi::Any {
        if (auto opt_map = value.try_cast<Map<String, ffi::Any>>()) {
          return reflection->CreateObject(type_key, opt_map.value());
        } else {
          auto opt_val = value.try_cast<ValueType>();
          if (!opt_val.has_value()) {
            TVM_FFI_THROW(AttributeError)
                << "Expect config " << key << " to have type " << type_key << ", but instead get "
                << ffi::details::AnyUnsafe::GetMismatchTypeInfo<ValueType>(value);
          }
          return *opt_val;
        }
      };
      RegisterConfigOption(key, type_key, legalization);
    } else {
      // non-object type, do not support implicit conversion from map
      std::string type_str = ffi::TypeTraits<ValueType>::TypeStr();
      auto legalization = [=](ffi::Any value) -> ffi::Any {
        auto opt_val = value.try_cast<ValueType>();
        if (!opt_val.has_value()) {
          TVM_FFI_THROW(AttributeError)
              << "Expect config " << key << " to have type " << type_str << ", but instead get "
              << ffi::details::AnyUnsafe::GetMismatchTypeInfo<ValueType>(value);
        } else {
          return *opt_val;
        }
      };
      RegisterConfigOption(key, type_str, legalization);
    }
    return 0;
  }

  // accessor.
  using ContainerType = PassContextNode;
  class Internal;

 private:
  // The entry of a pass context scope.
  TVM_DLL void EnterWithScope();
  // The exit of a pass context scope.
  TVM_DLL void ExitWithScope();
  // Register configuration key value type.
  TVM_DLL static void RegisterConfigOption(const char* key, String value_type_str,
                                           std::function<ffi::Any(ffi::Any)> legalization);

  // Classes to get the Python `with` like syntax.
  friend class Internal;
  friend class With<PassContext>;
};

#define TVM_PASS_CTX_CONFIG_VAR_DEF static TVM_ATTRIBUTE_UNUSED uint32_t __make_PassContext_tid

/*!
 * \brief Helper macro to register the object type to runtime.
 *  Makes sure that the runtime type table is correctly populated.
 *
 *  Use this macro in the cc file for each terminal class.
 */
#define TVM_REGISTER_PASS_CONFIG_OPTION(Key, ValueType)      \
  TVM_STR_CONCAT(TVM_PASS_CTX_CONFIG_VAR_DEF, __COUNTER__) = \
      ::tvm::transform::PassContext::RegisterConfigOption<ValueType>(Key)

/*!
 * \brief Meta data that will be used to help optimization and analysis.
 * \sa PassInfo
 */
class PassInfoNode : public Object {
 public:
  /*! \brief The minimal optimization level that this pass will be enabled. */
  int opt_level;

  /*! \brief The name of an optimization/analysis pass. */
  String name;

  /*! \brief Boolean that tells whether this pass will be traced or not. */
  bool traceable;

  /*! \brief The passes that are required to perform the current pass. */
  Array<String> required;

  PassInfoNode() = default;

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<PassInfoNode>()
        .def_ro("opt_level", &PassInfoNode::opt_level)
        .def_ro("name", &PassInfoNode::name)
        .def_ro("required", &PassInfoNode::required)
        .def_ro("traceable", &PassInfoNode::traceable);
  }

  static constexpr const char* _type_key = "transform.PassInfo";

  TVM_DECLARE_FINAL_OBJECT_INFO(PassInfoNode, Object);
};

/*!
 * \brief Managed reference class for PassInfoNode
 * \sa PassInfoNode
 */
class PassInfo : public ObjectRef {
 public:
  /*!
   * \brief Constructor
   * \param opt_level The optimization level
   * \param name Name of the pass.
   * \param required  The passes that are required to perform the current pass.
   * \param traceable Boolean that tells whether the pass is traceable.
   */
  TVM_DLL PassInfo(int opt_level, String name, Array<String> required, bool traceable);

  TVM_DEFINE_OBJECT_REF_METHODS(PassInfo, ObjectRef, PassInfoNode);
};

/*!
 * \brief PassNode is the base type of differnt types of optimization passes.
 * It is designed as a pure class and implemented by different pass subclasses
 * at different granularity of Relax nodes.
 */
class PassNode : public Object {
 public:
  virtual ~PassNode() {}
  /*!
   * \brief Get the pass information/meta data. */
  virtual PassInfo Info() const = 0;

  /*!
   * \brief Transform mod using the default PassContext in the current scope.
   *
   * \param mod The module that an optimization pass runs on.
   *
   * \return The transformed module.
   */
  IRModule operator()(IRModule mod) const {
    return this->operator()(std::move(mod), PassContext::Current());
  }

  /*!
   * \brief Transform mod using a functor under a given pass context.
   *
   * \param mod The module that an optimization pass runs on.
   * \param pass_ctx The pass context that can provide information for the optimization.
   *
   * \return The transformed module.
   */
  virtual IRModule operator()(IRModule mod, const PassContext& pass_ctx) const = 0;

  static constexpr const char* _type_key = "transform.Pass";
  TVM_DECLARE_BASE_OBJECT_INFO(PassNode, Object);
};

class Pass : public ObjectRef {
 public:
  /*!
   * \brief Transform mod using the default PassContext in the current scope.
   *
   * \code
   *
   * // If you do no longer need the input module
   * // it is recommended to use std::move to move your input module.
   * mod = pass(std::move(mod));
   *
   * \endcode
   *
   * \param mod The module that an optimization pass runs on.
   *
   * \return The transformed module.
   */
  IRModule operator()(IRModule mod) const;

  /*!
   * \brief Transform mod using a functor under a given pass context.
   *
   * \param mod The module that an optimization pass runs on.
   * \param pass_ctx The pass context that can provide information for the optimization.
   *
   * \return The transformed module.
   */
  IRModule operator()(IRModule mod, const PassContext& pass_ctx) const;

  TVM_DEFINE_OBJECT_REF_METHODS(Pass, ObjectRef, PassNode);

 private:
  IRModule static AssertImmutableModule(const IRModule& mod, const PassNode* node,
                                        const PassContext& pass_ctx);
};

/*!
 * \brief The SequentialNode contains a set of passes that transform Relax
 * programs from one AST to another semantically equivalent one.
 *
 * One example of this level of pass is that the pass manager needs to correctly
 * perform a host of optimizations with a given optimization level and disabled
 * passes.
 */
class SequentialNode : public PassNode {
 public:
  /* \brief The pass meta data.*/
  PassInfo pass_info;

  /*! \brief A list of passes that used to compose a sequential pass. */
  tvm::Array<Pass> passes;

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<SequentialNode>()
        .def_ro("pass_info", &SequentialNode::pass_info)
        .def_ro("passes", &SequentialNode::passes);
  }

  /*!
   * \brief Get the pass information/meta data.
   */
  PassInfo Info() const override { return pass_info; }

  /*!
   * \brief Resolve the pass dependency. It globs all required passes by
   *        a given pass and executes them.
   *
   * \param mod The module that an optimization pass runs on.
   *
   * TODO(zhiics) Build a dependency graph among the passes using provided
   * metadata, i.e. required_passes. Likely, we can have a data structure, i.e.
   * PassInfo, to store the relevant information including the parent passes.
   */
  void ResolveDependency(const IRModule& mod);

  /*!
   * \brief Perform optimizations on a series of passes. The aforementioned
   *        typical pass manager jobs could be done by it. This function could
   *        be overloaded to focus on different metrics, i.e. performance,
   *        memory footprint, etc.
   *
   * \param mod The module that these passes are applied on.
   * \param pass_ctx The context that these passes execute on.
   *
   * \return Return the updated module.
   */
  IRModule operator()(IRModule mod, const PassContext& pass_ctx) const final;

  static constexpr const char* _type_key = "transform.Sequential";
  TVM_DECLARE_FINAL_OBJECT_INFO(SequentialNode, PassNode);
};

class Sequential : public Pass {
 public:
  /*!
   * \brief The constructor of `Sequential`.
   *
   * \param passes The passes to apply.
   * \param pass_info The pass metadata.
   */
  TVM_DLL Sequential(Array<Pass> passes, PassInfo pass_info);

  /*!
   * \brief The constructor of `Sequential`.
   *
   * \param passes The passes to apply.
   * \param name The name of a sequential pass. It's defaulted to "sequential".
   *        This allows users to only provide a list of passes and execute them
   *        under a given context.
   */
  TVM_DLL Sequential(Array<Pass> passes, String name = "sequential");

  Sequential() = default;
  explicit Sequential(ObjectPtr<Object> n) : Pass(n) {}

  const SequentialNode* operator->() const;
  using ContainerType = SequentialNode;
};

/*
 * \brief Create a module pass.
 *
 * \param pass_func The packed function that contains the optimization.
 * \param opt_level The optimization level of the module pass.
 * \param name The name of the module pass.
 * \param required The list of the passes that the module pass is dependent on.
 *
 * \return The created module pass.
 */
TVM_DLL Pass CreateModulePass(std::function<IRModule(IRModule, PassContext)> pass_func,
                              int opt_level, String name, Array<String> required,
                              bool traceable = false);

/*
 * \brief Utility to apply a pass to specific functions in an IRModule
 *
 * TVM uses IRModule to IRModule transformations at all stages of
 * lowering.  These transformations may be useful when hand-writing an
 * optimized model, or to perform optimizations on specific kernels
 * within an IRModule.  This utility allows a pass to be applied to a
 * specified function, without altering other functions in the module.
 *
 * \param pass The IRModule to IRModule pass to be applied.
 *
 * \param func_name_regex A regex used to select the functions to be
 * updated.  The pass will be applied to all functions whose name
 * matches the regex.
 *
 * \param error_if_no_function_matches_regex Specifies the behavior if
 *     an IRModule does not contain any function matching the provided
 *     regex.  If true, an error will be raised.  If false (default),
 *     the IRModule will be returned unmodified.
 *
 * \return The modified IRModule to IRModule pass.
 */
TVM_DLL Pass ApplyPassToFunction(Pass pass, String func_name_regex,
                                 bool error_if_no_function_matches_regex = false);

/*!
 * \brief A special trace pass that prints the header and IR to LOG(INFO).
 * \param header The header to be attached to the output.
 * \param show_meta_data Whether should we show meta data.
 * \return The pass.
 */
TVM_DLL Pass PrintIR(String header = "", bool show_meta_data = false);

}  // namespace transform
}  // namespace tvm

#endif  // TVM_IR_TRANSFORM_H_
