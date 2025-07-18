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
 * \file src/relax/py_expr_functor.cc
 * \brief The backbone of PyExprVisitor/PyExprMutator.
 */
#include <tvm/ffi/reflection/registry.h>
#include <tvm/relax/expr_functor.h>

namespace tvm {
namespace relax {

/*!
 * \brief The abstract interface of ExprVisitor.
 */
class PyExprVisitorNode : public Object, public ExprVisitor {
 private:
  using TSelf = PyExprVisitorNode;
  using FType = tvm::NodeFunctor<void(const ObjectRef& n, TSelf* self)>;

 public:
  /*! \brief The packed function to the `VisitExpr(const Expr& expr)` function. */
  ffi::Function f_visit_expr{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const ConstantNode* op)` function. */
  ffi::Function f_visit_constant_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const TupleNode* op)` function. */
  ffi::Function f_visit_tuple_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const VarNode* op)` function. */
  ffi::Function f_visit_var_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const DataflowVarNode* op)` function. */
  ffi::Function f_visit_dataflow_var_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const ShapeExprNode* op)` function. */
  ffi::Function f_visit_shape_expr_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const ExternFuncNode* op)` function. */
  ffi::Function f_visit_extern_func_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const GlobalVarNode* op)` function. */
  ffi::Function f_visit_global_var_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const FunctionNode* op)` function. */
  ffi::Function f_visit_function_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const CallNode* op)` function. */
  ffi::Function f_visit_call_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const SeqExprNode* op)` function. */
  ffi::Function f_visit_seq_expr_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const IfNode* op)` function. */
  ffi::Function f_visit_if_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const OpNode* op)` function. */
  ffi::Function f_visit_op_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const TupleGetItemNode* op)` function. */
  ffi::Function f_visit_tuple_getitem_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const PrimValueNode* op)` function. */
  ffi::Function f_visit_prim_value_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const StringImmNode* op)` function. */
  ffi::Function f_visit_string_imm_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const DataTypeImmNode* op)` function. */
  ffi::Function f_visit_data_type_imm_{nullptr};
  /*! \brief The packed function to the `VisitBinding(const Binding& binding)` function. */
  ffi::Function f_visit_binding{nullptr};
  /*! \brief The packed function to the `VisitBinding_(const VarBindingNode* binding)`
   * function. */
  ffi::Function f_visit_var_binding_{nullptr};
  /*! \brief The packed function to the `VisitBinding_(const MatchCastNode* binding)`
   * function. */
  ffi::Function f_visit_match_cast_{nullptr};
  /*! \brief The packed function to the `VisitBindingBlock(const BindingBlock& block)`
   * function. */
  ffi::Function f_visit_binding_block{nullptr};
  /*! \brief The packed function to the `VisitBindingBlock_(const BindingBlockNode* block)`
   * function. */
  ffi::Function f_visit_binding_block_{nullptr};
  /*! \brief The packed function to the `VisitBindingBlock_(const DataflowBlockNode* block)`
   * function. */
  ffi::Function f_visit_dataflow_block_{nullptr};
  /*! \brief The packed function to the `VisitVarDef(const Var& var)` function. */
  ffi::Function f_visit_var_def{nullptr};
  /*! \brief The packed function to the `VisitVarDef_(const VarNode* var)` function. */
  ffi::Function f_visit_var_def_{nullptr};
  /*! \brief The packed function to the `VisitVarDef_(const DataflowVarNode* var)` function. */
  ffi::Function f_visit_dataflow_var_def_{nullptr};
  /*! \brief The packed function to the `VisitSpan(const Span& span)` function. */
  ffi::Function f_visit_span{nullptr};

  void VisitExpr(const Expr& expr) {
    if (f_visit_expr != nullptr) {
      f_visit_expr(expr);
    } else {
      // Need to init the overwrite VTable
      static FType vtable = InitVTable();
      vtable(expr, this);
    }
  }

  void VisitBinding(const Binding& binding)
      PY_EXPR_VISITOR_DEFAULT(binding, f_visit_binding, ExprVisitor::VisitBinding(binding));

  void VisitBinding_(const VarBindingNode* binding)
      PY_EXPR_VISITOR_DEFAULT(GetRef<VarBinding>(binding), f_visit_var_binding_,
                              ExprVisitor::VisitBinding_(binding));
  void VisitBinding_(const MatchCastNode* binding)
      PY_EXPR_VISITOR_DEFAULT(GetRef<MatchCast>(binding), f_visit_match_cast_,
                              ExprVisitor::VisitBinding_(binding));

  void VisitBindingBlock(const BindingBlock& block)
      PY_EXPR_VISITOR_DEFAULT(block, f_visit_binding_block, ExprVisitor::VisitBindingBlock(block));

  void VisitBindingBlock_(const BindingBlockNode* block)
      PY_EXPR_VISITOR_DEFAULT(GetRef<BindingBlock>(block), f_visit_binding_block_,
                              ExprVisitor::VisitBindingBlock_(block));
  void VisitBindingBlock_(const DataflowBlockNode* block)
      PY_EXPR_VISITOR_DEFAULT(GetRef<DataflowBlock>(block), f_visit_dataflow_block_,
                              ExprVisitor::VisitBindingBlock_(block));

  void VisitVarDef(const Var& var)
      PY_EXPR_VISITOR_DEFAULT(var, f_visit_var_def, ExprVisitor::VisitVarDef(var));
  void VisitVarDef_(const VarNode* var)
      PY_EXPR_VISITOR_DEFAULT(GetRef<Var>(var), f_visit_var_def_, ExprVisitor::VisitVarDef_(var));
  void VisitVarDef_(const DataflowVarNode* var)
      PY_EXPR_VISITOR_DEFAULT(GetRef<DataflowVar>(var), f_visit_dataflow_var_def_,
                              ExprVisitor::VisitVarDef_(var));

  void VisitSpan(const Span& span)
      PY_EXPR_VISITOR_DEFAULT(span, f_visit_span, ExprVisitor::VisitSpan(span));

  static void RegisterReflection() {
    // PyExprVisitorNode has no fields to register
  }

  static constexpr const char* _type_key = "expr_functor.PyExprVisitor";
  TVM_DECLARE_BASE_OBJECT_INFO(PyExprVisitorNode, Object);

 private:
  // initialize the vtable.
  static FType InitVTable() {
    FType vtable;
    // Set dispatch
    PY_EXPR_VISITOR_DISPATCH(ConstantNode, f_visit_constant_);
    PY_EXPR_VISITOR_DISPATCH(TupleNode, f_visit_tuple_);
    PY_EXPR_VISITOR_DISPATCH(VarNode, f_visit_var_);
    PY_EXPR_VISITOR_DISPATCH(DataflowVarNode, f_visit_dataflow_var_);
    PY_EXPR_VISITOR_DISPATCH(ShapeExprNode, f_visit_shape_expr_);
    PY_EXPR_VISITOR_DISPATCH(ExternFuncNode, f_visit_extern_func_);
    PY_EXPR_VISITOR_DISPATCH(GlobalVarNode, f_visit_global_var_);
    PY_EXPR_VISITOR_DISPATCH(FunctionNode, f_visit_function_);
    PY_EXPR_VISITOR_DISPATCH(CallNode, f_visit_call_);
    PY_EXPR_VISITOR_DISPATCH(SeqExprNode, f_visit_seq_expr_);
    PY_EXPR_VISITOR_DISPATCH(IfNode, f_visit_if_);
    PY_EXPR_VISITOR_DISPATCH(OpNode, f_visit_op_);
    PY_EXPR_VISITOR_DISPATCH(TupleGetItemNode, f_visit_tuple_getitem_);
    PY_EXPR_VISITOR_DISPATCH(PrimValueNode, f_visit_prim_value_);
    PY_EXPR_VISITOR_DISPATCH(StringImmNode, f_visit_string_imm_);
    PY_EXPR_VISITOR_DISPATCH(DataTypeImmNode, f_visit_data_type_imm_);
    vtable.Finalize();
    return vtable;
  }
};

TVM_REGISTER_NODE_TYPE(PyExprVisitorNode);

/*!
 * \brief Managed reference to PyExprVisitorNode.
 * \sa PyExprVisitorNode
 */
class PyExprVisitor : public ObjectRef {
 public:
  /*!
   * \brief Create a PyExprVisitor with customized methods on the python-side.
   * \param f_visit_expr The packed function of `VisitExpr(const Expr& expr)`.
   * \param f_visit_constant_ The packed function of `VisitExpr_(const ConstantNode* op)`.
   * \param f_visit_tuple_ The packed function of `VisitExpr_(const TupleNode* op)`.
   * \param f_visit_var_ The packed function of `VisitExpr_(const VarNode* op)`.
   * \param f_visit_dataflow_var_ The packed function of `VisitExpr_(const DataflowVarNode* op)`.
   * \param f_visit_shape_expr_ The packed function of `VisitExpr_(const ShapeExprNode* op)`.
   * \param f_visit_extern_func_ The packed function of `VisitExpr_(const ExternFuncNode* op)`.
   * \param f_visit_global_var_ The packed function of `VisitExpr_(const GlobalVarNode* op)`.
   * \param f_visit_function_ The packed function of `VisitExpr_(const FunctionNode* op)`.
   * \param f_visit_call_ The packed function of `VisitExpr_(const CallNode* op)`.
   * \param f_visit_seq_expr_ The packed function of `VisitExpr_(const SeqExprNode* op)`.
   * \param f_visit_if_ The packed function of `VisitExpr_(const IfNode* op)`.
   * \param f_visit_op_ The packed function of `VisitExpr_(const OpNode* op)`.
   * \param f_visit_tuple_getitem_ The packed function of `VisitExpr_(const TupleGetItemNode* op)`.
   * \param f_visit_prim_value_ The packed function of `VisitExpr_(const PrimValueNode* op)`.
   * \param f_visit_string_imm_ The packed function of `VisitExpr_(const StringImmNode* op)`.
   * \param f_visit_data_type_imm_ The packed function of `VisitExpr_(const DataTypeImmNode* op)`.
   * \param f_visit_binding The packed function of `VisitBinding(const Binding& binding)`.
   * \param f_visit_var_binding_ The packed function of `VisitBinding_(const VarBindingNode*
   * binding)`.
   * \param f_visit_match_cast_ The packed function of `VisitBinding_(const MatchCastNode*
   * binding)`.
   * \param f_visit_binding_block The packed function of `VisitBindingBlock(const BindingBlock&
   * block)`.
   * \param f_visit_binding_block_ The packed function of `VisitBindingBlock_(const
   * BindingBlockNode* block)`.
   * \param f_visit_dataflow_block_ The packed function of `VisitBindingBlock_(const
   * DataflowBlockNode* block)`.
   * \param f_visit_var_def The packed function of `VisitVarDef(const Var& var)`.
   * \param f_visit_var_def_ The packed function of `VisitVarDef_(const VarNode* var)`.
   * \param f_visit_dataflow_var_def_ The packed function of `VisitVarDef_(const DataflowVarNode*
   * var)`.
   * \param f_visit_span The packed function of `VisitSpan(const Span& span)`.
   * \return The PyVisitor created.
   */
  TVM_DLL static PyExprVisitor MakePyExprVisitor(
      ffi::Function f_visit_expr, ffi::Function f_visit_constant_, ffi::Function f_visit_tuple_,
      ffi::Function f_visit_var_, ffi::Function f_visit_dataflow_var_,
      ffi::Function f_visit_shape_expr_, ffi::Function f_visit_extern_func_,
      ffi::Function f_visit_global_var_, ffi::Function f_visit_function_,
      ffi::Function f_visit_call_, ffi::Function f_visit_seq_expr_, ffi::Function f_visit_if_,
      ffi::Function f_visit_op_, ffi::Function f_visit_tuple_getitem_,
      ffi::Function f_visit_prim_value_, ffi::Function f_visit_string_imm_,
      ffi::Function f_visit_data_type_imm_, ffi::Function f_visit_binding,
      ffi::Function f_visit_var_binding_, ffi::Function f_visit_match_cast_,
      ffi::Function f_visit_binding_block, ffi::Function f_visit_binding_block_,
      ffi::Function f_visit_dataflow_block_, ffi::Function f_visit_var_def,
      ffi::Function f_visit_var_def_, ffi::Function f_visit_dataflow_var_def_,
      ffi::Function f_visit_span) {
    ObjectPtr<PyExprVisitorNode> n = make_object<PyExprVisitorNode>();
    n->f_visit_expr = f_visit_expr;
    n->f_visit_binding = f_visit_binding;
    n->f_visit_binding_block = f_visit_binding_block;
    n->f_visit_var_def = f_visit_var_def;
    n->f_visit_span = f_visit_span;
    n->f_visit_constant_ = f_visit_constant_;
    n->f_visit_tuple_ = f_visit_tuple_;
    n->f_visit_var_ = f_visit_var_;
    n->f_visit_dataflow_var_ = f_visit_dataflow_var_;
    n->f_visit_shape_expr_ = f_visit_shape_expr_;
    n->f_visit_extern_func_ = f_visit_extern_func_;
    n->f_visit_global_var_ = f_visit_global_var_;
    n->f_visit_function_ = f_visit_function_;
    n->f_visit_call_ = f_visit_call_;
    n->f_visit_seq_expr_ = f_visit_seq_expr_;
    n->f_visit_if_ = f_visit_if_;
    n->f_visit_op_ = f_visit_op_;
    n->f_visit_tuple_getitem_ = f_visit_tuple_getitem_;
    n->f_visit_prim_value_ = f_visit_prim_value_;
    n->f_visit_string_imm_ = f_visit_string_imm_;
    n->f_visit_data_type_imm_ = f_visit_data_type_imm_;
    n->f_visit_var_binding_ = f_visit_var_binding_;
    n->f_visit_match_cast_ = f_visit_match_cast_;
    n->f_visit_binding_block_ = f_visit_binding_block_;
    n->f_visit_dataflow_block_ = f_visit_dataflow_block_;
    n->f_visit_var_def_ = f_visit_var_def_;
    n->f_visit_dataflow_var_def_ = f_visit_dataflow_var_def_;
    return PyExprVisitor(n);
  }

  TVM_DEFINE_MUTABLE_NOTNULLABLE_OBJECT_REF_METHODS(PyExprVisitor, ObjectRef, PyExprVisitorNode);
};

/*!
 * \brief The abstract interface of ExprMutator.
 */
class PyExprMutatorNode : public Object, public ExprMutator {
 private:
  using TSelf = PyExprMutatorNode;
  using FType = tvm::NodeFunctor<Expr(const ObjectRef& n, TSelf* self)>;

 public:
  /*! \brief The packed function to the `VisitExpr(const Expr& expr)` function. */
  ffi::Function f_visit_expr{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const ConstantNode* op)` function. */
  ffi::Function f_visit_constant_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const TupleNode* op)` function. */
  ffi::Function f_visit_tuple_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const VarNode* op)` function. */
  ffi::Function f_visit_var_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const DataflowVarNode* op)` function. */
  ffi::Function f_visit_dataflow_var_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const ShapeExprNode* op)` function. */
  ffi::Function f_visit_shape_expr_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const ExternFuncNode* op)` function. */
  ffi::Function f_visit_extern_func_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const GlobalVarNode* op)` function. */
  ffi::Function f_visit_global_var_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const FunctionNode* op)` function. */
  ffi::Function f_visit_function_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const CallNode* op)` function. */
  ffi::Function f_visit_call_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const SeqExprNode* op)` function. */
  ffi::Function f_visit_seq_expr_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const IfNode* op)` function. */
  ffi::Function f_visit_if_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const OpNode* op)` function. */
  ffi::Function f_visit_op_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const TupleGetItemNode* op)` function. */
  ffi::Function f_visit_tuple_getitem_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const PrimValueNode* op)` function. */
  ffi::Function f_visit_prim_value_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const StringImmNode* op)` function. */
  ffi::Function f_visit_string_imm_{nullptr};
  /*! \brief The packed function to the `VisitExpr_(const DataTypeImmNode* op)` function. */
  ffi::Function f_visit_data_type_imm_{nullptr};
  /*! \brief The packed function to the `VisitBinding(const Binding& binding)` function. */
  ffi::Function f_visit_binding{nullptr};
  /*! \brief The packed function to the `VisitBinding_(const VarBindingNode* binding)`
   * function. */
  ffi::Function f_visit_var_binding_{nullptr};
  /*! \brief The packed function to the `VisitBinding_(const MatchCastNode* binding)`
   * function. */
  ffi::Function f_visit_match_cast_{nullptr};
  /*! \brief The packed function to the `VisitBindingBlock(const BindingBlock& block)`
   * function. */
  ffi::Function f_visit_binding_block{nullptr};
  /*! \brief The packed function to the `VisitBindingBlock_(const BindingBlockNode* block)`
   * function. */
  ffi::Function f_visit_binding_block_{nullptr};
  /*! \brief The packed function to the `VisitBindingBlock_(const DataflowBlockNode* block)`
   * function. */
  ffi::Function f_visit_dataflow_block_{nullptr};
  /*! \brief The packed function to the `VisitVarDef(const Var& var)` function. */
  ffi::Function f_visit_var_def{nullptr};
  /*! \brief The packed function to the `VisitVarDef_(const VarNode* var)` function. */
  ffi::Function f_visit_var_def_{nullptr};
  /*! \brief The packed function to the `VisitVarDef_(const DataflowVarNode* var)` function. */
  ffi::Function f_visit_dataflow_var_def_{nullptr};
  /*! \brief The packed function to the `VisitSpan(const Span& span)` function. */
  ffi::Function f_visit_span{nullptr};

  Expr VisitExpr(const Expr& expr) {
    if (f_visit_expr != nullptr) {
      return builder_->Normalize(f_visit_expr(expr).cast<Expr>());
    } else {
      static FType vtable = InitVTable();
      return builder_->Normalize(vtable(expr, this));
    }
  }

  void VisitBinding(const Binding& binding) {
    if (f_visit_binding != nullptr)
      f_visit_binding(binding);
    else
      ExprMutator::VisitBinding(binding);
  }

  void VisitBinding_(const VarBindingNode* binding) {
    if (f_visit_var_binding_ != nullptr)
      f_visit_var_binding_(GetRef<VarBinding>(binding));
    else
      ExprMutator::VisitBinding_(binding);
  }

  void VisitBinding_(const MatchCastNode* binding) {
    if (f_visit_match_cast_ != nullptr)
      f_visit_match_cast_(GetRef<MatchCast>(binding));
    else
      ExprMutator::VisitBinding_(binding);
  }

  BindingBlock VisitBindingBlock(const BindingBlock& block)
      PY_EXPR_MUTATOR_DEFAULT(block, f_visit_binding_block, ExprMutator::VisitBindingBlock(block),
                              BindingBlock);

  BindingBlock VisitBindingBlock_(const BindingBlockNode* block)
      PY_EXPR_MUTATOR_DEFAULT(GetRef<BindingBlock>(block), f_visit_binding_block_,
                              ExprMutator::VisitBindingBlock_(block), BindingBlock);
  BindingBlock VisitBindingBlock_(const DataflowBlockNode* block)
      PY_EXPR_MUTATOR_DEFAULT(GetRef<DataflowBlock>(block), f_visit_dataflow_block_,
                              ExprMutator::VisitBindingBlock_(block), BindingBlock);

  Var VisitVarDef(const Var& var)
      PY_EXPR_MUTATOR_DEFAULT(var, f_visit_var_def, ExprMutator::VisitVarDef(var), Var);
  Var VisitVarDef_(const VarNode* var) PY_EXPR_MUTATOR_DEFAULT(GetRef<Var>(var), f_visit_var_def_,
                                                               ExprMutator::VisitVarDef_(var), Var);
  Var VisitVarDef_(const DataflowVarNode* var)
      PY_EXPR_MUTATOR_DEFAULT(GetRef<DataflowVar>(var), f_visit_dataflow_var_def_,
                              ExprMutator::VisitVarDef_(var), Var);

  /*!
   * \brief Dispatcher for post-order rewrite.
   * \param expr The Expr to be rewritten.
   * \return The Expr after post-order rewritten.
   */
  Expr VisitExprPostOrder(const Expr& expr) {
    static FType post_order_vtable = InitPostOrderVTable();
    return post_order_vtable(expr, this);
  }

  using ExprMutator::builder_;
  using ExprMutator::LookupBinding;
  using ExprMutator::var_remap_;
  using ExprMutator::VisitWithNewScope;
  using ExprMutator::WithStructInfo;

  static void RegisterReflection() {
    namespace refl = tvm::ffi::reflection;
    refl::ObjectDef<PyExprMutatorNode>().def_ro("builder_", &PyExprMutatorNode::builder_);
  }

  static constexpr const char* _type_key = "expr_functor.PyExprMutator";
  TVM_DECLARE_BASE_OBJECT_INFO(PyExprMutatorNode, Object);

 private:
  // initialize the vtable.
  static FType InitVTable() {
    FType vtable;
    // Set dispatch
    PY_EXPR_MUTATOR_DISPATCH(ConstantNode, f_visit_constant_);
    PY_EXPR_MUTATOR_DISPATCH(TupleNode, f_visit_tuple_);
    PY_EXPR_MUTATOR_DISPATCH(VarNode, f_visit_var_);
    PY_EXPR_MUTATOR_DISPATCH(DataflowVarNode, f_visit_dataflow_var_);
    PY_EXPR_MUTATOR_DISPATCH(ShapeExprNode, f_visit_shape_expr_);
    PY_EXPR_MUTATOR_DISPATCH(ExternFuncNode, f_visit_extern_func_);
    PY_EXPR_MUTATOR_DISPATCH(GlobalVarNode, f_visit_global_var_);
    PY_EXPR_MUTATOR_DISPATCH(FunctionNode, f_visit_function_);
    PY_EXPR_MUTATOR_DISPATCH(CallNode, f_visit_call_);
    PY_EXPR_MUTATOR_DISPATCH(SeqExprNode, f_visit_seq_expr_);
    PY_EXPR_MUTATOR_DISPATCH(IfNode, f_visit_if_);
    PY_EXPR_MUTATOR_DISPATCH(OpNode, f_visit_op_);
    PY_EXPR_MUTATOR_DISPATCH(TupleGetItemNode, f_visit_tuple_getitem_);
    PY_EXPR_MUTATOR_DISPATCH(PrimValueNode, f_visit_prim_value_);
    PY_EXPR_MUTATOR_DISPATCH(StringImmNode, f_visit_string_imm_);
    PY_EXPR_MUTATOR_DISPATCH(DataTypeImmNode, f_visit_data_type_imm_);
    vtable.Finalize();
    return vtable;
  }

  // initialize the vtable for post order visit.
  static FType InitPostOrderVTable() {
    FType post_order_vtable;
    // Set dispatch
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(ConstantNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(TupleNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(VarNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(DataflowVarNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(ShapeExprNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(ExternFuncNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(GlobalVarNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(FunctionNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(CallNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(SeqExprNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(IfNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(OpNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(TupleGetItemNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(PrimValueNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(StringImmNode);
    PY_EXPR_MUTATOR_VISIT_EXPR_POST_ORDER_DISPATCH(DataTypeImmNode);
    post_order_vtable.Finalize();
    return post_order_vtable;
  }
};

TVM_REGISTER_NODE_TYPE(PyExprMutatorNode);

/*!
 * \brief Managed reference to PyExprMutatorNode.
 * \sa PyExprMutatorNode
 */
class PyExprMutator : public ObjectRef {
 public:
  /*!
   * \brief Create a PyExprMutator with customized methods on the python-side.
   * \param f_visit_expr The packed function of `VisitExpr(const Expr& expr)`.
   * \param f_visit_constant_ The packed function of `VisitExpr_(const ConstantNode* op)`.
   * \param f_visit_tuple_ The packed function of `VisitExpr_(const TupleNode* op)`.
   * \param f_visit_var_ The packed function of `VisitExpr_(const VarNode* op)`.
   * \param f_visit_dataflow_var_ The packed function of `VisitExpr_(const DataflowVarNode* op)`.
   * \param f_visit_shape_expr_ The packed function of `VisitExpr_(const ShapeExprNode* op)`.
   * \param f_visit_extern_func_ The packed function of `VisitExpr_(const ExternFuncNode* op)`.
   * \param f_visit_global_var_ The packed function of `VisitExpr_(const GlobalVarNode* op)`.
   * \param f_visit_function_ The packed function of `VisitExpr_(const FunctionNode* op)`.
   * \param f_visit_call_ The packed function of `VisitExpr_(const CallNode* op)`.
   * \param f_visit_seq_expr_ The packed function of `VisitExpr_(const SeqExprNode* op)`.
   * \param f_visit_if_ The packed function of `VisitExpr_(const IfNode* op)`.
   * \param f_visit_op_ The packed function of `VisitExpr_(const OpNode* op)`.
   * \param f_visit_tuple_getitem_ The packed function of `VisitExpr_(const TupleGetItemNode* op)`.
   * \param f_visit_prim_value_ The packed function of `VisitExpr_(const PrimValueNode* op)`.
   * \param f_visit_string_imm_ The packed function of `VisitExpr_(const StringImmNode* op)`.
   * \param f_visit_data_type_imm_ The packed function of `VisitExpr_(const DataTypeImmNode* op)`.
   * \param f_visit_binding The packed function of `VisitBinding(const Binding& binding)`.
   * \param f_visit_var_binding_ The packed function of `VisitBinding_(const VarBindingNode*
   * binding)`.
   * \param f_visit_match_cast_ The packed function of `VisitBinding_(const MatchCastNode*
   * binding)`.
   * \param f_visit_binding_block The packed function of `VisitBindingBlock(const BindingBlock&
   * block)`.
   * \param f_visit_binding_block_ The packed function of `VisitBindingBlock_(const
   * BindingBlockNode* block)`.
   * \param f_visit_dataflow_block_ The packed function of `VisitBindingBlock_(const
   * DataflowBlockNode* block)`.
   * \param f_visit_var_def The packed function of `VisitVarDef(const Var& var)`.
   * \param f_visit_var_def_ The packed function of `VisitVarDef_(const VarNode* var)`.
   * \param f_visit_dataflow_var_def_ The packed function of `VisitVarDef_(const DataflowVarNode*
   * var)`.
   * \param f_visit_span The packed function of `VisitSpan(const Span& span)`.
   * \return The PyExprMutator created.
   */
  TVM_DLL static PyExprMutator MakePyExprMutator(
      BlockBuilder builder_, ffi::Function f_visit_expr, ffi::Function f_visit_constant_,
      ffi::Function f_visit_tuple_, ffi::Function f_visit_var_, ffi::Function f_visit_dataflow_var_,
      ffi::Function f_visit_shape_expr_, ffi::Function f_visit_extern_func_,
      ffi::Function f_visit_global_var_, ffi::Function f_visit_function_,
      ffi::Function f_visit_call_, ffi::Function f_visit_seq_expr_, ffi::Function f_visit_if_,
      ffi::Function f_visit_op_, ffi::Function f_visit_tuple_getitem_,
      ffi::Function f_visit_prim_value_, ffi::Function f_visit_string_imm_,
      ffi::Function f_visit_data_type_imm_, ffi::Function f_visit_binding,
      ffi::Function f_visit_var_binding_, ffi::Function f_visit_match_cast_,
      ffi::Function f_visit_binding_block, ffi::Function f_visit_binding_block_,
      ffi::Function f_visit_dataflow_block_, ffi::Function f_visit_var_def,
      ffi::Function f_visit_var_def_, ffi::Function f_visit_dataflow_var_def_,
      ffi::Function f_visit_span) {
    ObjectPtr<PyExprMutatorNode> n = make_object<PyExprMutatorNode>();
    n->builder_ = builder_;
    n->f_visit_expr = f_visit_expr;
    n->f_visit_constant_ = f_visit_constant_;
    n->f_visit_tuple_ = f_visit_tuple_;
    n->f_visit_var_ = f_visit_var_;
    n->f_visit_dataflow_var_ = f_visit_dataflow_var_;
    n->f_visit_shape_expr_ = f_visit_shape_expr_;
    n->f_visit_extern_func_ = f_visit_extern_func_;
    n->f_visit_global_var_ = f_visit_global_var_;
    n->f_visit_function_ = f_visit_function_;
    n->f_visit_call_ = f_visit_call_;
    n->f_visit_seq_expr_ = f_visit_seq_expr_;
    n->f_visit_if_ = f_visit_if_;
    n->f_visit_op_ = f_visit_op_;
    n->f_visit_tuple_getitem_ = f_visit_tuple_getitem_;
    n->f_visit_prim_value_ = f_visit_prim_value_;
    n->f_visit_string_imm_ = f_visit_string_imm_;
    n->f_visit_data_type_imm_ = f_visit_data_type_imm_;
    n->f_visit_binding = f_visit_binding;
    n->f_visit_var_binding_ = f_visit_var_binding_;
    n->f_visit_match_cast_ = f_visit_match_cast_;
    n->f_visit_binding_block = f_visit_binding_block;
    n->f_visit_binding_block_ = f_visit_binding_block_;
    n->f_visit_dataflow_block_ = f_visit_dataflow_block_;
    n->f_visit_var_def = f_visit_var_def;
    n->f_visit_var_def_ = f_visit_var_def_;
    n->f_visit_dataflow_var_def_ = f_visit_dataflow_var_def_;
    n->f_visit_span = f_visit_span;
    return PyExprMutator(n);
  }
  TVM_DEFINE_MUTABLE_NOTNULLABLE_OBJECT_REF_METHODS(PyExprMutator, ObjectRef, PyExprMutatorNode);
};

TVM_FFI_STATIC_INIT_BLOCK({
  namespace refl = tvm::ffi::reflection;
  refl::GlobalDef()
      .def("relax.MakePyExprVisitor", PyExprVisitor::MakePyExprVisitor)
      .def("relax.PyExprVisitorVisitExpr",
           [](PyExprVisitor visitor, const Expr& expr) { visitor->VisitExpr(expr); })
      .def("relax.PyExprVisitorVisitBinding",
           [](PyExprVisitor visitor, const Binding& binding) { visitor->VisitBinding(binding); })
      .def("relax.PyExprVisitorVisitBindingBlock",
           [](PyExprVisitor visitor, const BindingBlock& block) {
             visitor->VisitBindingBlock(block);
           })
      .def("relax.PyExprVisitorVisitVarDef",
           [](PyExprVisitor visitor, const Var& var) { visitor->VisitVarDef(var); })
      .def("relax.ExprVisitorVisitExpr",
           [](PyExprVisitor visitor, const Expr& expr) { visitor->ExprVisitor::VisitExpr(expr); })
      .def("relax.ExprVisitorVisitBinding",
           [](PyExprVisitor visitor, const Binding& binding) {
             if (const auto* ptr = binding.as<VarBindingNode>()) {
               visitor->ExprVisitor::VisitBinding_(ptr);
             } else if (const auto* ptr = binding.as<MatchCastNode>()) {
               visitor->ExprVisitor::VisitBinding_(ptr);
             } else {
               LOG(FATAL) << "unreachable";
             }
           })
      .def("relax.ExprVisitorVisitBindingBlock",
           [](PyExprVisitor visitor, const BindingBlock& block) {
             if (const auto* ptr = block.as<DataflowBlockNode>()) {
               visitor->ExprVisitor::VisitBindingBlock_(ptr);
             } else if (const auto* ptr = block.as<BindingBlockNode>()) {
               visitor->ExprVisitor::VisitBindingBlock_(ptr);
             } else {
               LOG(FATAL) << "TypeError: Invalid type: " << block->GetTypeKey();
             }
           })
      .def("relax.ExprVisitorVisitVarDef",
           [](PyExprVisitor visitor, const Var& var) {
             if (const auto* node = var.as<DataflowVarNode>()) {
               visitor->ExprVisitor::VisitVarDef_(node);
             } else if (const auto* node = var.as<VarNode>()) {
               visitor->ExprVisitor::VisitVarDef_(node);
             } else {
               LOG(FATAL) << "TypeError: Invalid type: " << var->GetTypeKey();
             }
           })
      .def("relax.ExprVisitorVisitSpan",
           [](PyExprVisitor visitor, const Span& span) { visitor->ExprVisitor::VisitSpan(span); })
      .def("relax.MakePyExprMutator", PyExprMutator::MakePyExprMutator)
      .def("relax.PyExprMutatorVisitExpr",
           [](PyExprMutator mutator, const Expr& expr) { return mutator->VisitExpr(expr); })
      .def("relax.PyExprMutatorVisitBinding",
           [](PyExprMutator mutator, const Binding& binding) { mutator->VisitBinding(binding); })
      .def("relax.PyExprMutatorVisitBindingBlock",
           [](PyExprMutator mutator, const BindingBlock& block) {
             return mutator->VisitBindingBlock(block);
           })
      .def("relax.PyExprMutatorVisitVarDef",
           [](PyExprMutator mutator, const Var& var) { return mutator->VisitVarDef(var); })
      .def("relax.ExprMutatorVisitExpr",
           [](PyExprMutator mutator, const Expr& expr) {
             return mutator->ExprMutator::VisitExpr(expr);
           })
      .def("relax.ExprMutatorVisitBinding",
           [](PyExprMutator mutator, const Binding& binding) {
             if (const auto* ptr = binding.as<VarBindingNode>()) {
               return mutator->ExprMutator::VisitBinding_(ptr);
             } else if (const auto* ptr = binding.as<MatchCastNode>()) {
               return mutator->ExprMutator::VisitBinding_(ptr);
             } else {
               LOG(FATAL) << "unreachable";
             }
           })
      .def("relax.ExprMutatorVisitBindingBlock",
           [](PyExprMutator mutator, const BindingBlock& block) {
             if (const auto* node = block.as<DataflowBlockNode>()) {
               return mutator->ExprMutator::VisitBindingBlock_(node);
             } else if (const auto* node = block.as<BindingBlockNode>()) {
               return mutator->ExprMutator::VisitBindingBlock_(node);
             } else {
               LOG(FATAL) << "TypeError: Invalid type: " << block->GetTypeKey();
             }
           })
      .def("relax.ExprMutatorVisitVarDef",
           [](PyExprMutator mutator, const Var& var) {
             if (const auto* node = var.as<DataflowVarNode>()) {
               return mutator->ExprMutator::VisitVarDef_(node);
             } else if (const auto* node = var.as<VarNode>()) {
               return mutator->ExprMutator::VisitVarDef_(node);
             } else {
               LOG(FATAL) << "TypeError: Invalid type: " << var->GetTypeKey();
             }
           })
      .def(
          "relax.PyExprMutatorVisitExprPostOrder",
          [](PyExprMutator mutator, const Expr& expr) { return mutator->VisitExprPostOrder(expr); })
      .def("relax.PyExprMutatorVisitWithNewScope",
           [](PyExprMutator mutator, const Expr& expr) { return mutator->VisitWithNewScope(expr); })
      .def("relax.PyExprMutatorLookupBinding",
           [](PyExprMutator mutator, const Var& var) { return mutator->LookupBinding(var); })
      .def("relax.PyExprMutatorWithStructInfo",
           [](PyExprMutator mutator, Var var, StructInfo sinfo) {
             return mutator->WithStructInfo(var, sinfo);
           })
      .def("relax.PyExprMutatorSetVarRemap",
           [](PyExprMutator mutator, Id id, Var var) { return mutator->var_remap_[id] = var; })
      .def("relax.PyExprMutatorGetVarRemap",
           [](PyExprMutator mutator, Id id) { return mutator->var_remap_[id]; });
});

TVM_FFI_STATIC_INIT_BLOCK({
  PyExprVisitorNode::RegisterReflection();
  PyExprMutatorNode::RegisterReflection();
});

}  // namespace relax
}  // namespace tvm
