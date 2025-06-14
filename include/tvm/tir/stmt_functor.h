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
 * \file tvm/tir/stmt_functor.h
 *
 * \brief Functors for tir stmts
 *        utility functions to call common functors.
 */
#ifndef TVM_TIR_STMT_FUNCTOR_H_
#define TVM_TIR_STMT_FUNCTOR_H_

#include <tvm/node/functor.h>
#include <tvm/tir/expr.h>
#include <tvm/tir/expr_functor.h>
#include <tvm/tir/function.h>
#include <tvm/tir/stmt.h>

#include <unordered_map>
#include <utility>

namespace tvm {
namespace tir {
/*!
 * \brief Same as ExprFunctor except it is applied on statements
 * \tparam FType The function signature.
 * \sa ExprFunctor
 */
template <typename FType>
class StmtFunctor;

#define STMT_FUNCTOR_DEFAULT \
  { return VisitStmtDefault_(op, std::forward<Args>(args)...); }

#define IR_STMT_FUNCTOR_DISPATCH(OP)                                                       \
  vtable.template set_dispatch<OP>([](const ObjectRef& n, TSelf* self, Args... args) {     \
    return self->VisitStmt_(static_cast<const OP*>(n.get()), std::forward<Args>(args)...); \
  });

template <typename R, typename... Args>
class StmtFunctor<R(const Stmt& n, Args... args)> {
 private:
  using TSelf = StmtFunctor<R(const Stmt& n, Args... args)>;
  using FType = NodeFunctor<R(const ObjectRef& n, TSelf* self, Args... args)>;

 public:
  /*! \brief the result type of this functor */
  using result_type = R;
  /*! \brief virtual destructor */
  virtual ~StmtFunctor() {}
  /*!
   * \brief Same as call.
   * \param n The stmt node.
   * \param args Additional arguments.
   * \return The result of the call
   */
  R operator()(const Stmt& n, Args... args) { return VisitStmt(n, std::forward<Args>(args)...); }
  /*!
   * \brief The functor call.
   * \param n The stmt node.
   * \param args Additional arguments.
   * \return The result of the call
   */
  virtual R VisitStmt(const Stmt& n, Args... args) {
    static FType vtable = InitVTable();
    return vtable(n, this, std::forward<Args>(args)...);
  }
  // Functions that can be overriden by subclass
  virtual R VisitStmt_(const LetStmtNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const AttrStmtNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const IfThenElseNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const ForNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const WhileNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const AllocateNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const AllocateConstNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const DeclBufferNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const BufferStoreNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const BufferRealizeNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const AssertStmtNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const SeqStmtNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const EvaluateNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const BlockNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmt_(const BlockRealizeNode* op, Args... args) STMT_FUNCTOR_DEFAULT;
  virtual R VisitStmtDefault_(const Object* op, Args...) {
    LOG(FATAL) << "Do not have a default for " << op->GetTypeKey();
    TVM_FFI_UNREACHABLE();
  }

 private:
  // initialize the vtable.
  static FType InitVTable() {
    FType vtable;
    IR_STMT_FUNCTOR_DISPATCH(LetStmtNode);
    IR_STMT_FUNCTOR_DISPATCH(AttrStmtNode);
    IR_STMT_FUNCTOR_DISPATCH(IfThenElseNode);
    IR_STMT_FUNCTOR_DISPATCH(ForNode);
    IR_STMT_FUNCTOR_DISPATCH(WhileNode);
    IR_STMT_FUNCTOR_DISPATCH(AllocateNode);
    IR_STMT_FUNCTOR_DISPATCH(AllocateConstNode);
    IR_STMT_FUNCTOR_DISPATCH(DeclBufferNode);
    IR_STMT_FUNCTOR_DISPATCH(AssertStmtNode);
    IR_STMT_FUNCTOR_DISPATCH(SeqStmtNode);
    IR_STMT_FUNCTOR_DISPATCH(EvaluateNode);
    IR_STMT_FUNCTOR_DISPATCH(BufferStoreNode);
    IR_STMT_FUNCTOR_DISPATCH(BufferRealizeNode);
    IR_STMT_FUNCTOR_DISPATCH(BlockNode);
    IR_STMT_FUNCTOR_DISPATCH(BlockRealizeNode);
    vtable.Finalize();
    return vtable;
  }
};

#undef IR_STMT_FUNCTOR_DISPATCH
#undef STMT_FUNCTOR_DEFAULT

/*!
 * \brief StmtVisitor.
 */
class TVM_DLL StmtVisitor : protected StmtFunctor<void(const Stmt&)> {
 public:
  using StmtFunctor::operator();

 protected:
  using StmtFunctor::VisitStmt;
  /*!
   * \brief Visitor to Exprs, can be overriden
   *        to do recursive changes to Exprs.
   * \note A common pattern is to call ExprVisitor here,
   *       or have a class sub-class both StmtVisitor and ExprVisitor
   *       and redirect Visit to ExprMutator::VisitExpr(Expr)
   */
  virtual void VisitExpr(const PrimExpr& e) {}
  // statement visitor
  void VisitStmt_(const AttrStmtNode* op) override;
  void VisitStmt_(const IfThenElseNode* op) override;
  void VisitStmt_(const LetStmtNode* op) override;
  void VisitStmt_(const ForNode* op) override;
  void VisitStmt_(const WhileNode* op) override;
  void VisitStmt_(const AllocateNode* op) override;
  void VisitStmt_(const AllocateConstNode* op) override;
  void VisitStmt_(const DeclBufferNode* op) override;
  void VisitStmt_(const BufferStoreNode* op) override;
  void VisitStmt_(const BufferRealizeNode* op) override;
  void VisitStmt_(const AssertStmtNode* op) override;
  void VisitStmt_(const SeqStmtNode* op) override;
  void VisitStmt_(const EvaluateNode* op) override;
  void VisitStmt_(const BlockNode* op) override;
  void VisitStmt_(const BlockRealizeNode* op) override;
};

/*!
 * \brief StmtMutator that mutates the statements.
 */
class TVM_DLL StmtMutator : protected StmtFunctor<Stmt(const Stmt&)> {
 public:
  /*!
   * \brief Mutate stmt.
   * \param stmt The input statement to be mutated.
   * \return The result of the call
   * \note It is important that stmt is passed by value.
   *       so copy on write can be triggered correctly.
   *       do mutator(std::move(stmt)) or when copy elison is triggered.
   */
  Stmt operator()(Stmt stmt) {
    allow_copy_on_write_ = true;
    return VisitStmt(stmt);
  }

 protected:
  // We perform copy on write optimizations on the StmtMutator
  // so that an unique copy of parent can be mutated inplace
  // when some of its children changed.
  // We only do such optimization for Stmt nests(instead of Exprs) for now
  // as Stmt's parent state is more likely remain unchanged when one of
  // its child block changes.
  /*!
   * \brief Internal state to indicate whether copy on write is enabled.
   *  COW is enabled iff all the parents of the node are unique.
   */
  bool allow_copy_on_write_{false};
  /*!
   * \brief Perform copy on write on node.
   *
   *  If CopyOnWrite is allowed, directly return
   *  a strong reference to the node container.
   *  Otherwise, return a copy of the node.
   *
   * \return The result object pointer.
   */
  template <typename TNode>
  ObjectPtr<TNode> CopyOnWrite(const TNode* node) {
    static_assert(std::is_base_of<StmtNode, TNode>::value,
                  "StmtMutator:: CopyOnWrite requires us to track uniqueness of all parent "
                  "nodes during the recursion. Because the child classes do not necessarily "
                  "check the Array, Expr and other structures during the visit, it is only safe to "
                  "call this function with StmtNodes for now. "
                  "Please create a new node directly in other cases.");
    if (allow_copy_on_write_) {
      // return the old node.
      return runtime::GetObjectPtr<TNode>(const_cast<TNode*>(node));
    } else {
      // Make a new copy of the node.
      // need to rely on the default copy constructor
      return ffi::make_object<TNode>(*node);
    }
  }
  /*!
   * \brief Internal mutator that everyone calls.
   * \note To override mutate's behavior, override VisitExpr instead.
   * \param stmt The input stmt.
   * \return The mutated results.
   */
  Stmt VisitStmt(const Stmt& stmt) override {
    if (allow_copy_on_write_ && !stmt.unique()) {
      allow_copy_on_write_ = false;
      Stmt ret = StmtFunctor::VisitStmt(stmt);
      allow_copy_on_write_ = true;
      return ret;
    } else {
      return StmtFunctor::VisitStmt(stmt);
    }
  }
  /*!
   * \brief Visitor to Exprs, can be overriden
   *        to do recursive changes to Exprs.
   * \note A common pattern is to call ExprMutator here,
   *       or have a class sub-class both StmtMutator and ExprMutator
   *       and redirect Mutate to ExprMutator::Mutate(Expr)
   */
  virtual PrimExpr VisitExpr(const PrimExpr& e) { return e; }
  // statement visitor
  Stmt VisitStmt_(const AttrStmtNode* op) override;
  Stmt VisitStmt_(const IfThenElseNode* op) override;
  Stmt VisitStmt_(const LetStmtNode* op) override;
  Stmt VisitStmt_(const ForNode* op) override;
  Stmt VisitStmt_(const WhileNode* op) override;
  Stmt VisitStmt_(const AllocateNode* op) override;
  Stmt VisitStmt_(const AllocateConstNode* op) override;
  Stmt VisitStmt_(const DeclBufferNode* op) override;
  Stmt VisitStmt_(const BufferStoreNode* op) override;
  Stmt VisitStmt_(const BufferRealizeNode* op) override;
  Stmt VisitStmt_(const AssertStmtNode* op) override;
  Stmt VisitStmt_(const SeqStmtNode* op) override;
  Stmt VisitStmt_(const EvaluateNode* op) override;
  Stmt VisitStmt_(const BlockNode* op) override;
  Stmt VisitStmt_(const BlockRealizeNode* op) override;
  /*!
   * \brief Alternative advance method for SeqStmtNode.
   *
   *  This function can be called when a child class override
   *  VisitStmt_(const SeqStmtNode*) to introduce
   *  the special behavior to visit
   *
   * \param op The sequence.
   * \param flatten_before_visit Whether to flatten the sequence before visit.
   * \param fmutate The mutate function, can be nullptr, which defaults to Visit.
   * \return The mutated result.
   */
  Stmt VisitSeqStmt_(const SeqStmtNode* op, bool flatten_before_visit,
                     std::function<Stmt(const Stmt&)> fmutate = nullptr);

  // internal helper.
  class Internal;
};

/*!
 * \brief Visitor that recursively visit stmts and exprs on them.
 */
class StmtExprVisitor : public StmtVisitor, public ExprVisitor {
 public:
  using StmtVisitor::operator();
  using ExprVisitor::operator();

 protected:
  using ExprVisitor::VisitExpr;
  using StmtVisitor::VisitStmt;

  void VisitExpr(const PrimExpr& e) override { return ExprVisitor::VisitExpr(e); }
};

/*!
 * \brief Mutator that recursively mutates stmts and exprs on them.
 */
class StmtExprMutator : public StmtMutator, public ExprMutator {
 public:
  using StmtMutator::operator();
  using ExprMutator::operator();

 protected:
  using ExprMutator::VisitExpr;
  using StmtMutator::VisitExpr;

  PrimExpr VisitExpr(const PrimExpr& e) override { return ExprMutator::VisitExpr(e); }
};

/*!
 * \brief recursively visit the ir nodes in post DFS order, and transform it
 *
 * \param stmt The ir to be transformed.
 * \param preorder The function called in before recursive mutation
 *          If preorder returns None, then the transform will proceed to recursive call.
 *          If preorder returns a not None Stmt/Expr, the transformer will simply return it and
 *          won't do further recursion.
 * \param postorder The function called after recursive mutation.
 *          The recursive mutation result is passed to postorder for further mutation.
 * \param only_enable List of String.
 *          If it is null, all IRNode will call preorder/postorder
 *          If it is not null, preorder/postorder will only be called
 *          when the IRNode's type key is in the list.
 */
TVM_DLL Stmt IRTransform(Stmt stmt, const ffi::Function& preorder, const ffi::Function& postorder,
                         Optional<Array<String>> only_enable = std::nullopt);

/*!
 * \brief Recursively visit the ir in post DFS order node, apply fvisit
 * Each node is guaranteed to be visited only once.
 * \param node The ir to be visited.
 * \param fvisit The visitor function to be applied.
 */
TVM_DLL void PostOrderVisit(const ObjectRef& node, std::function<void(const ObjectRef&)> fvisit);

/*!
 * \brief Substitute the var specified by vmap.
 * \param stmt The source statement to be substituted
 * \param vmap returns a new value if re-mapping is needed, otherwise returns nullptr.
 * \return The converted form.
 */
TVM_DLL Stmt Substitute(Stmt stmt, std::function<Optional<PrimExpr>(const Var& var)> vmap);

/*!
 * \brief Substitute the var specified by vmap.
 * \param expr The source statement to be substituted
 * \param vmap returns a new value if re-mapping is needed, otherwise returns nullptr.
 * \return The result.
 */
TVM_DLL PrimExpr Substitute(PrimExpr expr, std::function<Optional<PrimExpr>(const Var& var)> vmap);

/*!
 * \brief Substitute the var specified by vmap.
 * \param arr The array of Stmt/PrimExpr to be substituted
 * \param vmap returns a new value if re-mapping is needed, otherwise returns nullptr.
 * \return The result.
 */
template <typename T>
Array<T> Substitute(const Array<T>& arr, std::function<Optional<PrimExpr>(const Var& var)> vmap) {
  return arr.Map([&vmap](const auto& elem) { return Substitute(elem, vmap); });
}

/*!
 * \brief Substitute the vars specified by vmap.
 * \param range The array of Stmt/PrimExpr to be substituted
 * \param vmap returns a new value if re-mapping is needed, otherwise returns nullptr.
 * \return The modified Range.
 */
inline Range Substitute(const Range& range,
                        std::function<Optional<PrimExpr>(const Var& var)> vmap) {
  return Range::FromMinExtent(Substitute(range->min, vmap), Substitute(range->extent, vmap));
}

/*!
 * \brief Substitute the vars specified by vmap.
 *
 * Delegates to the Substitute methods that use std::function.  This
 * overload allows braced-initialization of the Map, whereas the
 * template<typename Expr> overload cannot.
 *
 * \param obj The object in which TIR variables should be substituted
 * \param vmap Map defining the TIR variables to be replaced
 * \return The modified object.
 */
template <typename Obj>
auto Substitute(Obj&& obj, const Map<Var, PrimExpr>& vmap) {
  auto func = [&vmap](const Var& var) -> Optional<PrimExpr> { return vmap.Get(var); };
  return Substitute(std::forward<Obj>(obj), func);
}

/*!
 * \brief Substitute the vars specified by vmap.
 *
 * Delegates to the Substitute methods that use std::function.
 *
 * \param obj The object in which TIR variables should be substituted
 * \param vmap Map defining the TIR variables to be replaced
 * \return The modified object.
 */
template <typename Obj, typename Expr,
          typename = std::enable_if_t<std::is_base_of_v<PrimExpr, Expr>>>
auto Substitute(Obj&& obj, const Map<Var, Expr>& vmap) {
  auto func = [&vmap](const Var& var) -> Optional<PrimExpr> {
    if (auto opt = vmap.Get(var)) {
      return opt.value();
    } else {
      return std::nullopt;
    }
  };
  return Substitute(std::forward<Obj>(obj), func);
}

/*!
 * \brief Substitute the vars specified by vmap.
 *
 * Delegates to the Substitute methods that use std::function.
 *
 * \param obj The object in which TIR variables should be substituted
 * \param vmap Map defining the TIR variables to be replaced
 * \return The modified object.
 */
template <typename Obj, typename Expr,
          typename = std::enable_if_t<std::is_base_of_v<PrimExpr, Expr>>>
auto Substitute(Obj&& obj, const std::unordered_map<const VarNode*, Expr>& vmap) {
  auto func = [&vmap](const Var& var) -> Optional<PrimExpr> {
    if (auto it = vmap.find(var.get()); it != vmap.end()) {
      return it->second;
    } else {
      return std::nullopt;
    }
  };
  return Substitute(std::forward<Obj>(obj), func);
}

/*!
 * \brief Substitute the vars specified by vmap.
 *
 * Delegates to the Substitute methods that use std::function.
 *
 * \param obj The object in which TIR variables should be substituted
 * \param vmap Map defining the TIR variables to be replaced
 * \return The modified object.
 */
template <typename Obj, typename Expr, typename Hasher, typename EqualityChecker,
          typename = std::enable_if_t<std::is_base_of_v<PrimExpr, Expr>>>
auto Substitute(Obj&& obj, const std::unordered_map<Var, Expr, Hasher, EqualityChecker>& vmap) {
  auto func = [&vmap](const Var& var) -> Optional<PrimExpr> {
    if (auto it = vmap.find(var); it != vmap.end()) {
      return it->second;
    } else {
      return std::nullopt;
    }
  };
  return Substitute(std::forward<Obj>(obj), func);
}

/*!
 * \brief Substitute the vars specified by vmap.
 *
 * Delegates to the Substitute methods that use std::function.
 *
 * \param obj The object in which TIR variables should be substituted
 * \param iter_vmap Map defining the TIR variables to be replaced
 * \return The modified object.
 */
template <typename Obj, typename Expr,
          typename = std::enable_if_t<std::is_base_of_v<PrimExpr, Expr>>>
auto Substitute(Obj&& obj, const std::unordered_map<IterVar, Expr>& iter_vmap) {
  std::unordered_map<const VarNode*, PrimExpr> vmap;
  for (const auto& [iter_var, expr] : iter_vmap) {
    vmap[iter_var->var.get()] = expr;
  }

  auto func = [&vmap](const Var& var) -> Optional<PrimExpr> {
    if (auto it = vmap.find(var.get()); it != vmap.end()) {
      return it->second;
    } else {
      return std::nullopt;
    }
  };
  return Substitute(std::forward<Obj>(obj), func);
}

/*!
 * \brief Substitute the var specified by vmap and legalize data types after substitution.
 * \param stmt The source statement to be substituted
 * \param vmap returns a new value if re-mapping is needed, otherwise returns nullptr.
 *
 * Unlike `Substitute`, this allows the substitution to change the data type of the expression.
 *
 * \sa Substitute
 * \return The result.
 */
TVM_DLL Stmt SubstituteWithDataTypeLegalization(Stmt stmt,
                                                std::function<Optional<PrimExpr>(const Var&)> vmap);

/*!
 * \brief Substitute the var specified by vmap and legalize data types after substitution.
 * \param expr The source statement to be substituted
 * \param vmap returns a new value if re-mapping is needed, otherwise returns nullptr.
 *
 * Unlike `Substitute`, this allows the substitution to change the data type of the expression.
 *
 * \sa Substitute
 * \return The result.
 */
TVM_DLL PrimExpr SubstituteWithDataTypeLegalization(
    PrimExpr expr, std::function<Optional<PrimExpr>(const Var&)> vmap);

/*!
 * \brief Recursively visit the IR in pre DFS order node, apply fvisit.
 * If fvisit returns false, it won't visit the children of the node.
 * \param stmt_or_expr The ir to be visited.
 * \param fvisit The visitor function to be applied. If fvisit returns false, it won't visit the
 * children of the node
 */
TVM_DLL void PreOrderVisit(const ObjectRef& stmt_or_expr,
                           const std::function<bool(const ObjectRef&)>& fvisit);

/*!
 * \brief Renew the definition nodes for a TIR, including Var, Buffer and IterVar.
 *        This pass works as a simple DeepCopy to duplicate a function with different Vars and
 *        Buffers but the same behavior
 * \param func The input PrimFunc.
 * \return The renewed func.
 */
TVM_DLL PrimFunc RenewDefs(const PrimFunc& func);

/*!
 * \brief Check if the statement contains the specified node type.
 *
 * This utility potentially walks the entire statement, and should
 * therefore not be used if it could otherwise be merged with another
 * pass.
 *
 * \param stmt The statement to be searched
 * \return Whether stmt contains Node
 */
template <typename Node, typename = std::enable_if_t<std::is_base_of_v<StmtNode, Node>>>
bool ContainsNode(const Stmt& stmt) {
  struct Visitor : StmtVisitor {
    // Early bail-out, if we already found the node.
    void VisitStmt(const Stmt& stmt) final {
      if (contains_node) {
        return;
      }
      StmtVisitor::VisitStmt(stmt);
    }

    void VisitStmt_(const Node* block) override { contains_node = true; }

    bool contains_node{false};
  };

  Visitor visitor;
  visitor(stmt);
  return visitor.contains_node;
}

}  // namespace tir
}  // namespace tvm

#endif  // TVM_TIR_STMT_FUNCTOR_H_
