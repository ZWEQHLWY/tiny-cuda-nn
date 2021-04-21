/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of
 *       conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TOR (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *//*
 */

/** @file   cross_entropy.h
 *  @author Thomas Müller, NVIDIA
 *  @brief  Implementation of the cross entropy loss and its gradient
 */

#pragma once

#include <tiny-cuda-nn/common.h>
#include <tiny-cuda-nn/gpu_matrix.h>
#include <tiny-cuda-nn/misc_kernels.h>
#include <tiny-cuda-nn/loss.h>


TCNN_NAMESPACE_BEGIN

template <typename T>
__global__ void cross_entropy_loss(
	const uint32_t n_elements,
	const uint32_t stride,
	const uint32_t dims,
	const float loss_scale,
	const T* __restrict__ predictions,
	const float* __restrict__ targets,
	float* __restrict__ values,
	T* __restrict__ gradients,
	const float* __restrict__ data_pdf = nullptr
) {
	const uint32_t i = threadIdx.x + blockIdx.x * blockDim.x;
	if (i >= n_elements) return;

	const uint32_t dim_idx = i % stride;
	const uint32_t elem_idx = i / stride;
	if (dim_idx >= dims) {
		values[i] = 0;
		gradients[i] = 0;
		return;
	}

	const uint32_t target_idx = elem_idx * dims + dim_idx;

	const uint32_t n_total = n_elements / stride * dims;

	const float prediction = (float)predictions[i];
	const float target = (float)targets[target_idx];

	const float pdf = data_pdf ? data_pdf[target_idx] : 1;
	const float factor = -target / pdf / n_total;

	const float value = factor * logf(prediction); // Epsilon to prevent NaNs
	const float gradient = factor / prediction;

	values[i] = (T)value;
	gradients[i] = (T)(loss_scale * gradient);
}


template <typename T>
class CrossEntropyLoss : public Loss<T> {
public:
	void evaluate(
		cudaStream_t stream,
		const uint32_t stride,
		const uint32_t dims,
		const float loss_scale,
		const GPUMatrix<T, MatrixLayout::ColumnMajor>& prediction,
		const GPUMatrix<float, MatrixLayout::ColumnMajor>& target,
		GPUMatrix<float, MatrixLayout::ColumnMajor>& values,
		GPUMatrix<T, MatrixLayout::ColumnMajor>& gradients,
		const GPUMatrix<float, MatrixLayout::ColumnMajor>* data_pdf = nullptr,
		const GPUMatrix<float, MatrixLayout::ColumnMajor>* data_factor = nullptr) const override {
		if (prediction.n() != target.n()) {
			throw std::runtime_error(std::string("Prediction and target don't have matching batch size ") + std::to_string(prediction.n()) + "!=" + std::to_string(target.n()));
		}

		if (prediction.m() != stride) {
			throw std::runtime_error(std::string("Prediction does not have appropriate dimensions ") + std::to_string(prediction.m()) + "!=" + std::to_string(stride));
		}

		if (target.m() != dims) {
			throw std::runtime_error(std::string("Target does not have appropriate dimensions ") + std::to_string(target.m()) + "!=" + std::to_string(dims));
		}

		linear_kernel(cross_entropy_loss<T>, 0, stream,
			prediction.n_elements(),
			stride,
			dims,
			loss_scale,
			prediction.data(),
			target.data(),
			values.data(),
			gradients.data(),
			data_pdf ? data_pdf->data() : nullptr
		);
	}

	void update_hyperparams(json params) override { }
};

TCNN_NAMESPACE_END
