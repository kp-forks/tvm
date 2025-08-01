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
 * \file src/relax/backend/contrib/cublas/codegen.cc
 * \brief Implementation of the CUBLAS JSON serializer.
 */
#include <tvm/ffi/reflection/registry.h>
#include <tvm/ir/module.h>
#include <tvm/runtime/builtin_fp16.h>

#include <string>

#include "../codegen_json/codegen_json.h"
#include "../utils.h"

namespace tvm {
namespace relax {
namespace contrib {

using JSONGraphNode = tvm::runtime::json::JSONGraphNode;
using JSONGraphNodeEntry = tvm::runtime::json::JSONGraphNodeEntry;
using JSONSerializer = backend::contrib::JSONSerializer;
using backend::contrib::NodeEntries;

class CublasJSONSerializer : public JSONSerializer {
 public:
  CublasJSONSerializer(Map<Constant, String> constant_names, Map<Var, Expr> bindings)
      : JSONSerializer(constant_names), bindings_(bindings) {}

  using JSONSerializer::VisitExpr_;

  NodeEntries VisitExpr_(const CallNode* call_node) final {
    const auto* fn_var = call_node->op.as<VarNode>();
    ICHECK(fn_var);
    const auto fn = Downcast<Function>(bindings_[GetRef<Var>(fn_var)]);
    ICHECK(fn.defined()) << "Expects the callee to be a function.";

    auto composite_opt = fn->GetAttr<String>(attr::kComposite);
    ICHECK(composite_opt.defined()) << "Only composite functions are supported.";

    std::string composite_name = composite_opt.value();

    NodeEntries inputs_tmp;
    for (const auto& arg : call_node->args) {
      auto res = VisitExpr(arg);
      inputs_tmp.insert(inputs_tmp.end(), res.begin(), res.end());
    }

    ICHECK(inputs_tmp.size() <= 4);
    NodeEntries inputs(inputs_tmp.size());

    auto arg_idx = backend::ExtractArgIdx(composite_name, fn);
    inputs[0] = inputs_tmp[arg_idx["lhs"]->value];
    inputs[1] = inputs_tmp[arg_idx["rhs"]->value];
    if (inputs_tmp.size() == 3) {
      inputs[2] = inputs_tmp[arg_idx["bias"]->value];
    } else if (inputs_tmp.size() == 4) {
      inputs[2] = inputs_tmp[arg_idx["scaleA"]->value];
      inputs[3] = inputs_tmp[arg_idx["scaleB"]->value];
    }

    auto node = std::make_shared<JSONGraphNode>(composite_name, /* name_ */
                                                "kernel",       /* op_type_ */
                                                inputs, 1 /* num_outputs_ */);
    if (composite_name.find("dequantize") != std::string::npos) {
      const CallNode* dequantize_call = backend::GetOpInFunction(fn, "relax.dequantize");
      if (dequantize_call->args[1]->IsInstance<ConstantNode>()) {
        const auto* const_expr = dequantize_call->args[1].as<ConstantNode>();
        auto sinfo = Downcast<TensorStructInfo>(const_expr->struct_info_);
        float alpha = 1.0;
        if (sinfo->dtype == DataType::Float(16)) {
          alpha = __gnu_h2f_ieee(static_cast<uint16_t*>(const_expr->data->data)[0]);
        } else {
          ICHECK(sinfo->dtype == DataType::Float(32));
          alpha = static_cast<float*>(const_expr->data->data)[0];
        }

        std::vector<std::string> dq_scale = {backend::to_str(alpha)};
        std::vector<dmlc::any> dq_scale_attr;
        dq_scale_attr.emplace_back(dq_scale);
        node->SetAttr("dq_scale", dq_scale_attr);
      }
    }

    const CallNode* root_call = backend::GetOpInFunction(fn, "relax.matmul");
    SetCallNodeAttribute(node, root_call);
    return AddNode(node, GetRef<Expr>(call_node));
  }

 private:
  /*! \brief The bindings to look up composite functions. */
  Map<Var, Expr> bindings_;
};

Array<runtime::Module> CublasCompiler(Array<Function> functions, Map<String, ffi::Any> /*unused*/,
                                      Map<Constant, String> constant_names) {
  Array<runtime::Module> compiled_functions;

  for (const auto& func : functions) {
    CublasJSONSerializer serializer(constant_names, AnalyzeVar2Value(func));
    serializer.serialize(func);
    auto graph_json = serializer.GetJSON();
    auto constant_names = serializer.GetConstantNames();
    const auto pf = tvm::ffi::Function::GetGlobalRequired("runtime.CublasJSONRuntimeCreate");
    auto func_name = GetExtSymbol(func);
    compiled_functions.push_back(pf(func_name, graph_json, constant_names).cast<runtime::Module>());
  }

  return compiled_functions;
}

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef().def("relax.ext.cublas", CublasCompiler);
});

}  // namespace contrib
}  // namespace relax
}  // namespace tvm
