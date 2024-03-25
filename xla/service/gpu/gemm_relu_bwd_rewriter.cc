/* Copyright 2019 The OpenXLA Authors.

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

#include "xla/service/gpu/gemm_relu_bwd_rewriter.h"

#include <cstdint>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "xla/hlo/ir/dfs_hlo_visitor_with_default.h"
#include "xla/hlo/ir/hlo_computation.h"
#include "xla/hlo/ir/hlo_instruction.h"
#include "xla/literal_util.h"
#include "xla/service/gpu/backend_configs.pb.h"
#include "xla/service/gpu/cublas_cudnn.h"
#include "xla/service/pattern_matcher.h"
#include "tsl/platform/errors.h"
#include "tsl/platform/statusor.h"

namespace xla {
namespace gpu {

namespace m = match;

class GemmReLUBwdVisitor : public DfsHloRewriteVisitor {
 public:
 auto CublasLtMatmulF8(HloInstruction **instr) {
  return m::CustomCall(
      instr, {kCublasLtMatmulF8CallTarget});
}
absl::Status ReluConvertDF8(HloInstruction *instr, HloInstruction *fwd_gemm,
                          HloInstruction *d_scale, HloInstruction *clamp_lower,
                          HloInstruction *clamp_upper, HloInstruction *maximum) {
    std::cout << "shuw:in side F8CnvertD\n";
    // Verify the data types and the operands of clamp.
    if (instr->shape().element_type() == F8E4M3FN) {
      if (!clamp_lower->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e4m3fn>::lowest())) ||
          !clamp_upper->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e4m3fn>::max()))) {
        return absl::OkStatus();
      }
    } else if (instr->shape().element_type() == F8E5M2) {
      if (!clamp_lower->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e5m2>::lowest())) ||
          !clamp_upper->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e5m2>::max()))) {
        return absl::OkStatus();
      }
    } else {
      return absl::OkStatus();
    }

    if (!ShapeUtil::IsScalar(d_scale->shape())) {
      return absl::OkStatus();
    }

    // The possible second user of the GEMM must be the calculation of the
    // maximum of the absolute value of the result of the GEMM. Since it is
    // unknown in what form this operation will be used, it is identified in a
    // top-down approach by inspecting the users of the GEMM.
    const std::vector<HloInstruction *> gemm_users = fwd_gemm->users();
    std::cout << "shuw: fwd_gemm" << fwd_gemm->ToString()<<std::endl;
    HloInstruction *reduce_damax = nullptr;
    HloInstruction *compare = nullptr;
    HloInstruction *select = nullptr;
    if (gemm_users.size() == 2) {
      // Assume the user of fwd gemm are maximum and compare, due to what happens in gemm_rewriter.
      // In the presence of a ReLU activation, the abs instruction is elided
      // since abs(ReLU(x)) = ReLU(x).
      TF_ASSIGN_OR_RETURN(auto gpu_config,
                          fwd_gemm->backend_config<GpuBackendConfig>());
      const GemmBackendConfig &config = gpu_config.gemm_backend_config();
      for (int i = 0; i < gemm_users.size(); ++i) {
        HloInstruction *maybe_reduce = nullptr;
        HloInstruction *maybe_compare = nullptr;
        HloInstruction *maybe_select = nullptr;
        if (gemm_users[i]->opcode() == HloOpcode::kMaximum) {
          // Assume the maximum has 2 users, reduce and divide
          if (gemm_users[i]->users().size() != 2) return absl::OkStatus();
          for (int j = 0; i < gemm_users[i]->users().size(); ++j) {
            if (gemm_users[i]->users()[j]->opcode() == HloOpcode::kReduce) {
              maybe_reduce = gemm_users[i]->users()[j];
              std::cout << "shuw: 111111\n";
              reduce_damax = maybe_reduce;
              break;
            }
          }
          std::cout << "shuw: 444444444444\n";
        } else if (gemm_users[i]->opcode() == HloOpcode::kCompare) {
          std::cout << "shuw: 333333333\n";
          maybe_compare = gemm_users[i];
          maybe_select = gemm_users[i]->users()[0];
                  select = maybe_select;
        compare = maybe_compare;
          std::cout << "shuw: 222222222\n";
        }
 
      }
   
 std::cout << "shuw: end of for\n";
 std::cout << reduce_damax->ToString() <<std::endl;
 std::cout << "shuw: end of reduce_damax\n";
 std::cout << select->ToString() <<std::endl;
  std::cout << "shuw: end of select\n";
 std::cout << compare->ToString() <<std::endl;
 std::cout << "shuw: end of for compare\n";
      if (!reduce_damax || !select || !compare) {
        return absl::OkStatus();
      }
    } else {
      return absl::OkStatus();
    }
std::cout << "shuw:in side F8CnvertD 11111111111\n";
    HloInstruction *bwd_gemm = nullptr;
    if (!Match(
            select,
            m::Select(
                m::Compare(m::CustomCall({kCublasLtMatmulF8CallTarget}),
                           m::Broadcast(m::ConstantScalar(0)))
                    .WithOneUser(),
                m::CustomCall(&bwd_gemm, {kCublasLtMatmulF8CallTarget})
                    .WithOneUser(),
                m::Broadcast(m::ConstantScalar(0))))) {
      return absl::OkStatus();
    }

   // Step (a), replace maximum with RELU_AUX 
   // Step (b), deal with select
   // Step (c), add dmax to output
    TF_ASSIGN_OR_RETURN(auto gpu_config,
                        fwd_gemm->backend_config<GpuBackendConfig>());
    GemmBackendConfig &config = *gpu_config.mutable_gemm_backend_config();
std::cout << "shuw:in side F8CnvertD 2222222222222\n";
    // Step (a), replace maximum with RELU_AUX 
    if (config.epilogue() == GemmBackendConfig::DEFAULT) {
      config.set_epilogue(GemmBackendConfig::RELU_AUX);
    } else if (config.epilogue() == GemmBackendConfig::BIAS) {
      config.set_epilogue(GemmBackendConfig::BIAS_RELU_AUX);
    } else {
      return absl::OkStatus();
    }
    auto total_elements = [](const HloInstruction *gemm) {
      int64_t num_e = 1;
      for (int i = 0; i < gemm->shape().rank(); ++i) {
        num_e *= gemm->shape().dimensions(i);
      }
      return num_e;
    };
    Shape mask_shape =
        ShapeUtil::MakeShape(PrimitiveType::U8, {total_elements(fwd_gemm)});
    mask_shape.mutable_layout()->set_element_size_in_bits(1);
    std::unique_ptr<HloInstruction> output = fwd_gemm->CloneWithNewShape(
        ShapeUtil::MakeTupleShape({fwd_gemm->shape(), mask_shape}));
    TF_RETURN_IF_ERROR(output->set_backend_config(gpu_config));
    // TF_RETURN_IF_ERROR(SetName(output->GetModule(), output.get()));
        std::cout << "shuw:in side F8CnvertD 33333333333333333333333\n";

    //  return absl::OkStatus();
    HloInstruction *tuple_output =
        fwd_gemm->parent()->AddInstruction(std::move(output));
    TF_RETURN_IF_ERROR(ReplaceWithNewInstruction(
        maximum, HloInstruction::CreateGetTupleElement(tuple_output, 0)));

    HloInstruction *get_tuple1 = fwd_gemm->parent()->AddInstruction(
        HloInstruction::CreateGetTupleElement(tuple_output, 1));
        std::cout << "shuw:in side F8CnvertD 44444444444444444\n";
    // Step (b), deal with select to have DRELu in bwd_gemm
std::vector<HloInstruction *> operands(bwd_gemm->operands().begin(),
                                        bwd_gemm->operands().end());
    operands.insert(operands.end(), get_tuple1);

    HloInstruction *new_bwd_custom_call =
        bwd_gemm->parent()->AddInstruction(HloInstruction::CreateCustomCall(
            ShapeUtil::MakeShapeWithDenseLayout(
                bwd_gemm->shape().element_type(),
                bwd_gemm->shape().dimensions(),
                bwd_gemm->shape().layout().minor_to_major()),
            operands, kCublasLtMatmulF8CallTarget));

    TF_ASSIGN_OR_RETURN(auto bwd_gpu_backend_config,
                        bwd_gemm->backend_config<GpuBackendConfig>());
    GemmBackendConfig &bwd_config =
        *bwd_gpu_backend_config.mutable_gemm_backend_config();
    bwd_config.set_epilogue(GemmBackendConfig::D_RELU);
    TF_RETURN_IF_ERROR(
        new_bwd_custom_call->set_backend_config(bwd_gpu_backend_config));

    TF_RETURN_IF_ERROR(ReplaceInstruction(select, new_bwd_custom_call));
    std::cout << "shuw: bwd_gemm" << new_bwd_custom_call->ToString()<<std::endl;
  // Step (c) add dmax to output 


    // If present, elide the calculation of the maximum of the absolute values
    // of the result of the GEMM.
    // Damax is sure there
    return RewriteFwdBwdGemm(tuple_output, new_bwd_custom_call, reduce_damax, instr); // instr is convert
    

    // std::unique_ptr<HloInstruction> new_gemm =
    //     fwd_gemm->CloneWithNewShape(instr->shape());
    // TF_RETURN_IF_ERROR(ReplaceWithNewInstruction(instr, std::move(new_gemm)));

    return absl::OkStatus();
  }

  // Adds a scalar DAmax return value to an FP8 GEMM.
  absl::Status RewriteFwdBwdGemm(HloInstruction *fwd_gemm, 
                          HloInstruction *bwd_gemm,
                          HloInstruction *fwd_reduce_damax, HloInstruction *next_fwd_convert) {
    
        std::cout << "shuw:in side F8CnvertD 5555555555555555\n";

      std::cout << "fwd_gemm users="<<fwd_gemm->users().size()<<std::endl;
    //   // Assume reduce, bwd_gemm, and divide-clamp-convert for next fwd gemm are 3 users of fwd_gemm(via get-tuple-element)

    // Change the output shape of the fwd_gemm Custom Call to tuple(D, bitmask, DAmax) from (D, bitmask).
    Shape damax_shape = ShapeUtil::MakeScalarShape(F32);
    std::cout << "fwd_gemm="<<fwd_gemm->ToString()<<std::endl;
     std::cout << "new tuple shape="<<fwd_gemm->shape().tuple_shapes(0)<<std::endl;
      std::cout << "new tuple shape="<<fwd_gemm->shape().tuple_shapes(1)<<std::endl;
    Shape tuple_shape =
        ShapeUtil::MakeTupleShape({next_fwd_convert->shape(), fwd_gemm->shape().tuple_shapes(1), damax_shape});
    HloInstruction *gemm_and_damax =
        fwd_gemm->AddInstruction(fwd_gemm->CloneWithNewShape(tuple_shape));
  std::cout << "shuw:in side F8CnvertD 66666666666666666\n";
    // Obtain D and DAmax separately from the output tuple.
    HloInstruction *d =
        fwd_gemm->AddInstruction(HloInstruction::CreateGetTupleElement(
            next_fwd_convert->shape(), gemm_and_damax, 0));
    HloInstruction *bitmask = fwd_gemm->AddInstruction(
        HloInstruction::CreateGetTupleElement(fwd_gemm->shape().tuple_shapes(1), gemm_and_damax, 1));            
    HloInstruction *damax = fwd_gemm->AddInstruction(
        HloInstruction::CreateGetTupleElement(damax_shape, gemm_and_damax, 2));
  std::cout << "shuw:in side F8CnvertD 777777777777777777777\n";
    TF_RETURN_IF_ERROR(ReplaceInstruction(fwd_reduce_damax, damax));
      std::cout << "shuw:in side F8CnvertD 88888888888888888888\n";
    TF_RETURN_IF_ERROR(ReplaceInstruction(next_fwd_convert, d));
      std::cout << "shuw:in side F8CnvertD 9999999999999999999\n";
    // Replace bwd_gemm's last operand
    std::vector<HloInstruction *> bwd_gemm_operands(bwd_gemm->operands().begin(),
                                                    bwd_gemm->operands().end());
    bwd_gemm_operands.back() = bitmask;

    HloInstruction *new_bwd_gemm = bwd_gemm->AddInstruction(
        bwd_gemm->CloneWithNewOperands(bwd_gemm->shape(), bwd_gemm_operands));

    TF_RETURN_IF_ERROR(ReplaceInstruction(bwd_gemm,new_bwd_gemm));
std::cout << "shuw:in side F8CnvertD 10000000000000000\n";
    return absl::OkStatus();
  }

  absl::Status HandleConvert(HloInstruction *instr) override {
    HloInstruction *clamp_lower, *clamp_upper, *d_scale, *existing_gemm, *binary,
        *maximum;
    if (Match(
            instr,
            m::Convert(m::Clamp(
                m::Broadcast(m::ConstantScalar(&clamp_lower)),
                m::AnyOf<HloInstruction>(
                    m::Divide(
                        &binary,
                        m::Maximum(&maximum,
                                   m::CustomCall(&existing_gemm,
                                                 {kCublasLtMatmulF8CallTarget}),
                                   m::Broadcast(m::ConstantScalar(0))),
                        m::Broadcast(m::Op(&d_scale))),
                    m::MultiplyAnyOrder(
                        &binary,
                        m::Maximum(&maximum,
                                   m::CustomCall(&existing_gemm,
                                                 {kCublasLtMatmulF8CallTarget}),
                                   m::Broadcast(m::ConstantScalar(0))),
                        m::Broadcast(m::Op(&d_scale)))),
                m::Broadcast(m::ConstantScalar(&clamp_upper)))))) {
      return ReluConvertDF8(instr, existing_gemm, d_scale, clamp_lower, clamp_upper,
                            maximum);
     } else if (Match(
                   instr,
                   m::Convert(
                       m::Clamp(
                           m::Broadcast(m::ConstantScalar(&clamp_lower)),
                           m::AnyOf<HloInstruction>(
                               m::Divide(
                                   &binary,
                                   m::CustomCall(&existing_gemm,
                                                 {kCublasLtMatmulF8CallTarget}),
                                   m::Broadcast(m::Op(&d_scale))),
                               m::MultiplyAnyOrder(
                                   &binary,
                                   m::CustomCall(&existing_gemm,
                                                 {kCublasLtMatmulF8CallTarget}),
                                   m::Broadcast(m::Op(&d_scale)))),
                           m::Broadcast(m::ConstantScalar(&clamp_upper)))
                           .WithOneUser()))) {
      return F8ConvertD(
          instr, existing_gemm, d_scale, clamp_lower, clamp_upper,
          /*mult_scale=*/binary->opcode() == HloOpcode::kMultiply);
    }
    return absl::OkStatus();
  }

absl::Status F8ConvertD(HloInstruction *instr, HloInstruction *existing_gemm,
                          HloInstruction *d_scale, HloInstruction *clamp_lower,
                          HloInstruction *clamp_upper,
                          bool mult_scale = false) {
    // Verify the data types and the operands of clamp.
    if (instr->shape().element_type() == F8E4M3FN) {
      if (!clamp_lower->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e4m3fn>::lowest())) ||
          !clamp_upper->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e4m3fn>::max()))) {
        return absl::OkStatus();
      }
    } else if (instr->shape().element_type() == F8E5M2) {
      if (!clamp_lower->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e5m2>::lowest())) ||
          !clamp_upper->literal().IsAllFloat(static_cast<float>(
              std::numeric_limits<tsl::float8_e5m2>::max()))) {
        return absl::OkStatus();
      }
    } else {
      return absl::OkStatus();
    }

    if (!ShapeUtil::IsScalar(d_scale->shape())) {
      return absl::OkStatus();
    }

    // The possible second user of the GEMM must be the calculation of the
    // maximum of the absolute value of the result of the GEMM. Since it is
    // unknown in what form this operation will be used, it is identified in a
    // top-down approach by inspecting the users of the GEMM.
    const std::vector<HloInstruction *> gemm_users = existing_gemm->users();
    HloInstruction *reduce_damax = nullptr;
    if (gemm_users.size() == 2) {
      // In the presence of a ReLU activation, the abs instruction is elided
      // since abs(ReLU(x)) = ReLU(x).
      TF_ASSIGN_OR_RETURN(auto gpu_config,
                          existing_gemm->backend_config<GpuBackendConfig>());
      const GemmBackendConfig &config = gpu_config.gemm_backend_config();
      for (int i = 0; i < gemm_users.size(); ++i) {
        HloInstruction *maybe_reduce = nullptr;
        if (gemm_users[i]->opcode() == HloOpcode::kAbs) {
          if (gemm_users[i]->users().size() != 1) continue;
          maybe_reduce = gemm_users[i]->users()[0];
        } else {
          // If there is no Abs instruction, relu is required as epilogue to
          // ensure all values are nonnegative.
          if (config.epilogue() != GemmBackendConfig::BIAS_RELU &&
              config.epilogue() != GemmBackendConfig::RELU)
            continue;
          maybe_reduce = gemm_users[i];
        }

        if (maybe_reduce->opcode() == HloOpcode::kReduce &&
            maybe_reduce->operands().size() == 2 &&
            maybe_reduce->operand(1)->opcode() == HloOpcode::kConstant &&
            ShapeUtil::IsScalar(maybe_reduce->operand(1)->shape())) {
          HloInstruction *reduce = maybe_reduce;
          HloComputation *reduce_comp = reduce->to_apply();
          HloInstruction *reduce_comp_root = reduce_comp->root_instruction();
          if (reduce->operand(1)->literal().GetAsDouble({}) <= 0. &&
              reduce_comp_root->opcode() == HloOpcode::kMaximum &&
              reduce_comp_root->operand(0)->opcode() == HloOpcode::kParameter &&
              reduce_comp_root->operand(1)->opcode() == HloOpcode::kParameter) {
            reduce_damax = reduce;
          }
        }
      }
      if (!reduce_damax) {
        return absl::OkStatus();
      }
    } else if (gemm_users.size() > 2) {
      return absl::OkStatus();
    }

    TF_ASSIGN_OR_RETURN(auto gpu_backend_config,
                        existing_gemm->backend_config<GpuBackendConfig>());
    const GemmBackendConfig &gemm_backend_config =
        gpu_backend_config.gemm_backend_config();

    if (gemm_backend_config.beta() != 0.0 &&
        existing_gemm->operand(2)->shape().element_type() != BF16 &&
        existing_gemm->operand(2)->shape().element_type() != F16) {
      VLOG(1) << "The scaling and conversion of the result of "
              << existing_gemm->ToShortString()
              << " is not fused into the FP8 Custom Call because it "
                 "conflicts with the existing fusion of the addition of a "
                 "matrix bias with element type other than BF16 or F16.";
      return absl::OkStatus();
    }

    // If necessary, invert the scaling factor of D and convert to F32.
    if (!mult_scale) {
      Literal one_literal = LiteralUtil::One(d_scale->shape().element_type());
      HloInstruction *one = instr->AddInstruction(
          HloInstruction::CreateConstant(one_literal.Clone()));
      d_scale = instr->AddInstruction(HloInstruction::CreateBinary(
          d_scale->shape(), HloOpcode::kDivide, one, d_scale));
    }
    if (d_scale->shape().element_type() != F32) {
      d_scale = instr->AddInstruction(HloInstruction::CreateConvert(
          ShapeUtil::MakeScalarShape(F32), d_scale));
    }
    TF_RETURN_IF_ERROR(existing_gemm->ReplaceOperandWith(
        gemm_backend_config.beta() == 0.0 ? 5 : 6, d_scale));

    // If present, elide the calculation of the maximum of the absolute values
    // of the result of the GEMM.
    if (reduce_damax) {
      return F8AddDAmax(instr, existing_gemm, reduce_damax);
    }

    std::unique_ptr<HloInstruction> new_gemm =
        existing_gemm->CloneWithNewShape(instr->shape());
    TF_RETURN_IF_ERROR(ReplaceWithNewInstruction(instr, std::move(new_gemm)));

    return absl::OkStatus();
  }

  // Adds a scalar DAmax return value to an FP8 GEMM.
  absl::Status F8AddDAmax(HloInstruction *instr, HloInstruction *existing_gemm,
                          HloInstruction *reduce_damax) {
    // Change the output shape of the Custom Call to tuple(D, DAmax).
    Shape damax_shape = ShapeUtil::MakeScalarShape(F32);
    Shape tuple_shape =
        ShapeUtil::MakeTupleShape({instr->shape(), damax_shape});
    HloInstruction *gemm_and_damax =
        instr->AddInstruction(existing_gemm->CloneWithNewShape(tuple_shape));

    // Obtain D and DAmax separately from the output tuple.
    HloInstruction *d =
        instr->AddInstruction(HloInstruction::CreateGetTupleElement(
            instr->shape(), gemm_and_damax, 0));
    HloInstruction *damax = instr->AddInstruction(
        HloInstruction::CreateGetTupleElement(damax_shape, gemm_and_damax, 1));

    // Convert DAmax from FP32 to the requested type and elide reduce.
    HloInstruction *damax_converted = instr->AddInstruction(
        HloInstruction::CreateConvert(reduce_damax->shape(), damax));
    TF_RETURN_IF_ERROR(ReplaceInstruction(reduce_damax, damax_converted));
    TF_RETURN_IF_ERROR(ReplaceInstruction(instr, d));

    return absl::OkStatus();
  }

 /****
  absl::Status HandleSelect(HloInstruction *instr) override {
    HloInstruction *fwd_gemm = nullptr;
    HloInstruction *bwd_gemm = nullptr;
    HloInstruction *bwd_gemm2 = nullptr;
    HloInstruction *get_tuple = nullptr;
    // std::cout << "55555555555555555\n";
    if (Match(instr, m::Select(m::Bitcast(m::Convert(m::GetTupleElement(&get_tuple, m::CustomCall(&fwd_gemm, {kCublasLtMatmulCallTarget}), 1))),
    m::CustomCall(&bwd_gemm,{kCublasLtMatmulCallTarget}),
    m::Broadcast(m::Constant())))) {
        std::cout << "RRRRRRRRRRR\n";
    std::cout << get_tuple->ToString()<<std::endl;
     
     bwd_gemm2 = instr->users()[0];
    std::vector<HloInstruction *> operands_list = {bwd_gemm->mutable_operand(0),
                                                   bwd_gemm->mutable_operand(1),
                                                   get_tuple};

    TF_ASSIGN_OR_RETURN(auto bwd_gpu_backend_config,
                        bwd_gemm->backend_config<GpuBackendConfig>());
    GemmBackendConfig &bwd_config = *bwd_gpu_backend_config.mutable_gemm_backend_config();
    bwd_config.set_epilogue(GemmBackendConfig::D_RELU);
    std::cout << "777777777777777\n";
    std::cout << bwd_gemm->mutable_operand(0)->ToString()<<std::endl;
    std::cout << "88888888888\n";
     std::cout << bwd_gemm->mutable_operand(1)->ToString()<<std::endl;
    HloInstruction *new_bwd_custom_call =
        bwd_gemm->parent()->AddInstruction(HloInstruction::CreateCustomCall(
            ShapeUtil::MakeShapeWithDenseLayout(
                bwd_gemm->shape().element_type(), bwd_gemm->shape().dimensions(),
                bwd_gemm->shape().layout().minor_to_major()),
            operands_list, kCublasLtMatmulCallTarget));
            std::cout << "ssssss111111\n";
            std::cout << new_bwd_custom_call->ToString()<<std::endl;
    TF_RETURN_IF_ERROR(new_bwd_custom_call->set_backend_config(bwd_gpu_backend_config));
    std::cout << "ssssss2222222222\n";
    return ReplaceInstruction(instr, new_bwd_custom_call);
    
    }
    return absl::OkStatus();
  }
*/
//   absl::Status HandleCustomCall(HloInstruction *instr) override {
//     HloInstruction *existing_gemm;
//     HloInstruction *bcast;
//     if (Match(instr, m::CustomCall(&existing_gemm,
//                                    {kGemmCallTarget, kCublasLtMatmulCallTarget})
//                          .WithOperand(0, m::Broadcast(&bcast, m::Op()))) ||
//         (Match(instr, m::CustomCall(&existing_gemm, {kGemmCallTarget,
//                                                      kCublasLtMatmulCallTarget})
//                           .WithOperand(1, m::Broadcast(&bcast, m::Op()))))) {
//       TF_ASSIGN_OR_RETURN(auto gpu_config,
//                           existing_gemm->backend_config<GpuBackendConfig>());
//       GemmBackendConfig &config = *gpu_config.mutable_gemm_backend_config();
//       DotDimensionNumbers *dim_nums = config.mutable_dot_dimension_numbers();
//       int bcast_operand_index = instr->operand_index(bcast);
//       int num_bcast_dims = (bcast->shape().dimensions_size() -
//                             bcast->operand(0)->shape().dimensions_size());
//       int num_batch_dims = dim_nums->lhs_batch_dimensions_size();

//       const tsl::protobuf::RepeatedField<int64_t> &batch_dimensions =
//           (bcast_operand_index == 1) ? dim_nums->rhs_batch_dimensions()
//                                      : dim_nums->lhs_batch_dimensions();
//       // This optimization is only valid if the set of broadcasted dimensions
//       // is exactly the set of batch dimensions. First, check that all newly
//       // broadcast dimensions have been inserted on the left i.e. all new
//       // dimensions must be in [0, num_bcast_dims) or equivalently all original
//       // dimensions are >= num_bcast_dims.
//       for (int64_t bcast_dim : bcast->dimensions()) {
//         if (bcast_dim < num_bcast_dims) {
//           return absl::OkStatus();
//         }
//         // bcast_dim should not be in batch_dimensions.
//         if (absl::c_linear_search(batch_dimensions, bcast_dim)) {
//           return absl::OkStatus();
//         }
//       }

//       // Then check that all batch dimensions are being broadcast, and that
//       // there is at least one batch dimension.
//       CHECK_GT(num_bcast_dims, 0);
//       if (num_bcast_dims != num_batch_dims) {
//         return absl::OkStatus();
//       }

//       if (bcast_operand_index == 1) {
//         CHECK_EQ(dim_nums->rhs_contracting_dimensions_size(), 1);
//         dim_nums->set_rhs_contracting_dimensions(
//             0, dim_nums->rhs_contracting_dimensions(0) - num_batch_dims);
//         dim_nums->clear_rhs_batch_dimensions();
//       } else {
//         CHECK_EQ(dim_nums->lhs_contracting_dimensions_size(), 1);
//         dim_nums->set_lhs_contracting_dimensions(
//             0, dim_nums->lhs_contracting_dimensions(0) - num_batch_dims);
//         dim_nums->clear_lhs_batch_dimensions();
//       }
//       TF_RETURN_IF_ERROR(existing_gemm->ReplaceOperandWithDifferentShape(
//           bcast_operand_index, bcast->mutable_operand(0)));
//       TF_RETURN_IF_ERROR(existing_gemm->set_backend_config(gpu_config));
//       MarkAsChanged();
//     }
//     return absl::OkStatus();
//   }
};

static absl::StatusOr<bool> RunOnComputation(HloComputation *computation) {
  GemmReLUBwdVisitor visitor;
  TF_RETURN_IF_ERROR(computation->Accept(&visitor));
  return visitor.changed();
}

absl::StatusOr<bool> GemmReLUBwdRewriter::Run(
    HloModule *module,
    const absl::flat_hash_set<absl::string_view> &execution_threads) {
  bool changed = false;
  for (HloComputation *computation :
       module->MakeNonfusionComputations(execution_threads)) {
    TF_ASSIGN_OR_RETURN(bool result, RunOnComputation(computation));
    changed |= result;
  }
  return changed;
}

}  // namespace gpu
}  // namespace xla