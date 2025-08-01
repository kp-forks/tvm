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
#include <tvm/ffi/function.h>
#include <tvm/ffi/reflection/accessor.h>
#include <tvm/ffi/reflection/registry.h>
#include <tvm/node/reflection.h>
#include <tvm/runtime/logging.h>
#include <tvm/script/printer/ir_docsifier.h>

#include <sstream>

#include "./utils.h"

namespace tvm {
namespace script {
namespace printer {

TVM_FFI_STATIC_INIT_BLOCK({
  FrameNode::RegisterReflection();
  IRDocsifierNode::RegisterReflection();
});

IdDoc IRDocsifierNode::Define(const ObjectRef& obj, const Frame& frame, const String& name_hint) {
  if (auto it = obj2info.find(obj); it != obj2info.end()) {
    // TVM's IR dialects do not allow multiple definitions of the same
    // variable within an IRModule.  This branch can only be reached
    // when printing ill-formed inputs.
    //
    // However, the printer is different from most utilities, as it
    // may neither assume that its input is well-formed, nor may it
    // throw an exception if the input is ill-formed.  The printer is
    // often used for debugging, where logging and printouts of an
    // IRModule are essential.  In these cases, throwing an error
    // would prevent a developer from determining why an IRModule is
    // ill-formed.
    return IdDoc(it->second.name.value());
  }

  String name = name_hint;
  if (cfg->show_object_address) {
    std::stringstream stream;
    stream << name << "_" << obj.get();
    name = stream.str();
  }
  name = GenerateUniqueName(name, this->defined_names);
  this->defined_names.insert(name);
  DocCreator doc_factory = [name]() { return IdDoc(name); };
  obj2info.insert({obj, VariableInfo{std::move(doc_factory), name}});
  IdDoc def_doc(name);
  frame->AddExitCallback([this, obj]() { this->RemoveVar(obj); });
  return def_doc;
}

void IRDocsifierNode::Define(const ObjectRef& obj, const Frame& frame, DocCreator doc_factory) {
  ICHECK(obj2info.find(obj) == obj2info.end()) << "Duplicated object: " << obj;
  obj2info.insert({obj, VariableInfo{std::move(doc_factory), std::nullopt}});
  frame->AddExitCallback([this, obj]() { this->RemoveVar(obj); });
}

Optional<ExprDoc> IRDocsifierNode::GetVarDoc(const ObjectRef& obj) const {
  auto it = obj2info.find(obj);
  if (it == obj2info.end()) {
    return std::nullopt;
  }
  return it->second.creator();
}

ExprDoc IRDocsifierNode::AddMetadata(const ffi::Any& obj) {
  ICHECK(obj != nullptr) << "TypeError: Cannot add nullptr to metadata";
  String key = obj.GetTypeKey();
  Array<ffi::Any>& array = metadata[key];
  int index = std::find_if(array.begin(), array.end(),
                           [&](const ffi::Any& a) { return ffi::AnyEqual()(a, obj); }) -
              array.begin();
  if (index == static_cast<int>(array.size())) {
    array.push_back(obj);
  }
  return IdDoc(
      "metadata")[{LiteralDoc::Str(key, std::nullopt)}][{LiteralDoc::Int(index, std::nullopt)}];
}

void IRDocsifierNode::AddGlobalInfo(const String& name, const GlobalInfo& ginfo) {
  ICHECK(ginfo.defined()) << "TypeError: Cannot add nullptr to global_infos";
  Array<GlobalInfo>& array = global_infos[name];
  array.push_back(ginfo);
}

bool IRDocsifierNode::IsVarDefined(const ObjectRef& obj) const { return obj2info.count(obj); }

void IRDocsifierNode::RemoveVar(const ObjectRef& obj) {
  auto it = obj2info.find(obj);
  ICHECK(it != obj2info.end()) << "No such object: " << obj;
  if (it->second.name.has_value()) {
    defined_names.erase(it->second.name.value());
  }
  obj2info.erase(it);
}

void IRDocsifierNode::SetCommonPrefix(const ObjectRef& root,
                                      ffi::TypedFunction<bool(ObjectRef)> is_var) {
  class Visitor {
   public:
    void operator()(ObjectRef obj) { this->VisitObjectRef(obj); }

   private:
    void RecursiveVisitAny(ffi::Any* value) {
      if (std::optional<ObjectRef> opt = value->as<ObjectRef>()) {
        this->VisitObjectRef(*opt);
      }
    }
    void VisitObjectRef(ObjectRef obj) {
      if (!obj.defined()) {
        return;
      }
      if (visited_.count(obj.get())) {
        if (is_var(obj)) {
          HandleVar(obj.get());
        }
        return;
      }
      visited_.insert(obj.get());
      stack_.push_back(obj.get());
      if (obj->IsInstance<ffi::ArrayObj>()) {
        const ffi::ArrayObj* array = static_cast<const ffi::ArrayObj*>(obj.get());
        for (Any element : *array) {
          this->RecursiveVisitAny(&element);
        }
      } else if (obj->IsInstance<ffi::MapObj>()) {
        const ffi::MapObj* map = static_cast<const ffi::MapObj*>(obj.get());
        for (std::pair<Any, Any> kv : *map) {
          this->RecursiveVisitAny(&kv.first);
          this->RecursiveVisitAny(&kv.second);
        }
      } else {
        const TVMFFITypeInfo* tinfo = TVMFFIGetTypeInfo(obj->type_index());
        if (tinfo->metadata != nullptr) {
          ffi::reflection::ForEachFieldInfo(tinfo, [&](const TVMFFIFieldInfo* field_info) {
            Any field_value = ffi::reflection::FieldGetter(field_info)(obj);
            this->RecursiveVisitAny(&field_value);
          });
        }
      }
      if (is_var(obj)) {
        HandleVar(obj.get());
      }
      stack_.pop_back();
    }

    void HandleVar(const Object* var) {
      if (common_prefix.count(var) == 0) {
        common_prefix[var] = stack_;
        return;
      }
      std::vector<const Object*>& a = common_prefix[var];
      std::vector<const Object*>& b = stack_;
      int n = std::min(a.size(), b.size());
      for (int i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
          a.resize(i);
          break;
        }
      }
    }

    ReflectionVTable* vtable_ = ReflectionVTable::Global();
    std::vector<const Object*> stack_;
    std::unordered_set<const Object*> visited_;

   public:
    ffi::TypedFunction<bool(ObjectRef)> is_var;
    std::unordered_map<const Object*, std::vector<const Object*>> common_prefix;
  };
  Visitor visitor;
  visitor.is_var = is_var;
  visitor(root);
  this->common_prefix = std::move(visitor.common_prefix);
}

IRDocsifier::IRDocsifier(const PrinterConfig& cfg) {
  auto n = make_object<IRDocsifierNode>();
  n->cfg = cfg;
  n->dispatch_tokens.push_back("");
  // Define builtin keywords according to cfg.
  for (const String& keyword : cfg->GetBuiltinKeywords()) {
    n->defined_names.insert(keyword);
  }
  data_ = std::move(n);
}

IRDocsifier::FType& IRDocsifier::vtable() {
  static IRDocsifier::FType inst;
  return inst;
}

TVM_REGISTER_NODE_TYPE(FrameNode);
TVM_REGISTER_NODE_TYPE(IRDocsifierNode);

TVM_STATIC_IR_FUNCTOR(IRDocsifier, vtable)
    .set_fallback([](ObjectRef obj, ObjectPath p, IRDocsifier d) -> Doc {
      return d->AddMetadata(obj);
    });

}  // namespace printer
}  // namespace script
}  // namespace tvm
