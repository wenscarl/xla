/* Copyright 2022 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/


#ifndef MLIR_HLO_DIALECT_MHLO_IR_HLO_OPS_TYPEDEFS
#define MLIR_HLO_DIALECT_MHLO_IR_HLO_OPS_TYPEDEFS

def MHLO_AsyncBundleTypeDef : TypeDef<MHLO_Dialect, "AsyncBundle"> {
  let mnemonic = "async_bundle";
  let summary = "Opaque collection of other types";
  let parameters = (ins ArrayRefParameter<"Type">:$types);
  let genStorageClass = 0;
  let assemblyFormat = "`<` params `>`";
  let extraClassDeclaration = [{

    // Return the number of held types.
    size_t size() const;

    // Iterate over the held elements.
    using iterator = ArrayRef<Type>::iterator;
    iterator begin() const { return getTypes().begin(); }
    iterator end() const { return getTypes().end(); }

    // Return the element type at index 'index'.
    Type getType(size_t index) const {
      assert(index < size() && "invalid index for tuple type");
      return getTypes()[index];
    }

    // Puts a flattened list of types in this bundle into `types`
    void getFlattenedTypes(SmallVectorImpl<Type> &types);
    static SmallVector<Type, 10> getFlattenedTypes(AsyncBundleType t) {
      SmallVector<Type, 10> fTypes;
      t.getFlattenedTypes(fTypes);
      return fTypes;
    }

  }];
}

// Whether a type is a AsyncBundleType.
def MHLO_IsAsyncBundleTypePred : CPred<"isa<::mlir::mhlo::AsyncBundleType>($_self)">;

def MHLO_AsyncBundle :
    MixedContainerType<AnyTypeOf<[MHLO_Tensor, MHLO_Token]>, MHLO_IsAsyncBundleTypePred,
                       "AsyncBundleType::getFlattenedTypes(cast<::mlir::mhlo::AsyncBundleType>($_self))",
                       "async_bundle">;

#endif // MLIR_HLO_DIALECT_MHLO_IR_HLO_OPS_TYPEDEFS
