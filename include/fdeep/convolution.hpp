// Copyright 2016, Tobias Hermann.
// https://github.com/Dobiasd/frugally-deep
// Distributed under the MIT License.
// (See accompanying LICENSE file or at
//  https://opensource.org/licenses/MIT)

#pragma once

#include "fdeep/common.hpp"

#include "fdeep/filter.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <numeric>
#include <vector>

namespace fdeep { namespace internal
{

// todo: Remove. Just save raw filters on layers.
struct im2col_filter_matrix
{
    shape5 filter_shape_;
    std::size_t filter_count_;
    std::vector<filter> filters_;
    std::vector<float_type> biases_;
    bool use_bias_;
};

inline im2col_filter_matrix generate_im2col_filter_matrix(
    const std::vector<filter>& filters)
{
    assertion(fplus::all_the_same_on(
        fplus_c_mem_fn_t(filter, shape, shape5), filters),
        "all filters must have the same shape");

    const auto biases = fplus::transform(
        fplus_c_mem_fn_t(filter, get_bias, float_type),
        filters);

    const bool use_bias =
        fplus::sum(biases) != static_cast<float_type>(0) ||
        !fplus::all_the_same(biases);

    return {filters.front().shape(), filters.size(), filters, biases, use_bias};
}

inline im2col_filter_matrix generate_im2col_single_filter_matrix(
    const filter& filter)
{
    return generate_im2col_filter_matrix(filter_vec(1, filter));
}

FDEEP_FORCE_INLINE float_type dot_product(
    const float_type* xs,
    const float_type* ys,
    std::size_t n)
{
    Eigen::Map<Eigen::Matrix<float_type, Eigen::Dynamic, 1>> vx(const_cast<float_type*>(xs), static_cast<EigenIndex>(n));
    Eigen::Map<Eigen::Matrix<float_type, Eigen::Dynamic, 1>> vy(const_cast<float_type*>(ys), static_cast<EigenIndex>(n));
    return vx.adjoint() * vy;
}

FDEEP_FORCE_INLINE tensor5 convolve_accumulative(
    std::size_t out_height,
    std::size_t out_width,
    std::size_t strides_y,
    std::size_t strides_x,
    const im2col_filter_matrix& filter_mat,
    std::size_t dot_product_dims,
    const tensor5& in)
{
    assertion(in.shape().rank() <= 3, "invalid rank for input tensor");

    const std::vector<filter>& filters = filter_mat.filters_;
    const auto f_height = filter_mat.filter_shape_.height_;
    const auto f_width = filter_mat.filter_shape_.width_;
    const auto f_depth = filter_mat.filter_shape_.depth_;
    const auto out_depth = filters.size();

    assertion(f_depth == in.shape().depth_, "filter depth does not match input");
    tensor5 output(shape5(1, 1, out_height, out_width, out_depth), static_cast<float_type>(0));
    assertion(dot_product_dims == f_width * f_depth, "invalid dot-product dimensions");

    //std::cout << dot_product_dims << ": " << filter_mat.filter_shape_.volume() << " vs. " << in.shape().volume() << " vs. " << out_depth * out_height * out_width << std::endl;

    // todo: Use fplus::transform_parallelly on z_out.
    for (std::size_t z_out = 0; z_out < out_depth; ++z_out)
    {
        const filter& current_filter = filters[z_out];
        for (std::size_t y_filt = 0; y_filt < f_height; ++y_filt)
        {
            const float_type* filter_ptr = &(current_filter.get_tensor5().get_ref(0, 0, y_filt, 0, 0));
            for (std::size_t y = 0, y_out = 0; y < in.shape().height_ + 1 - f_height; y += strides_y, ++y_out)
            {
                for (std::size_t x = 0, x_out = 0; x < in.shape().width_ + 1 - f_width; x += strides_x, ++x_out)
                {
                    const float_type* input_ptr = &in.get_ref(0, 0, y + y_filt, x, 0);
                    output.get_ref(0, 0, y_out, x_out, z_out) += dot_product(filter_ptr, input_ptr, dot_product_dims);
                }
            }
        }
    }

    if (filter_mat.use_bias_) {
        for (std::size_t y_out = 0; y_out < out_height; ++y_out)
        {
            for (std::size_t x_out = 0; x_out < out_width; ++x_out)
            {
                for (std::size_t z_out = 0; z_out < out_depth; ++z_out)
                {
                    output.get_ref(0, 0, y_out, x_out, z_out) += filter_mat.biases_[z_out];
                }
            }
        }
    }
    return output;
}

inline tensor5 convolve_accumulative_fix(
    std::size_t out_height,
    std::size_t out_width,
    std::size_t strides_y,
    std::size_t strides_x,
    const im2col_filter_matrix& filter_mat,
    std::size_t dot_product_dims,
    const tensor5& in)
{
    if (strides_y == 1 && strides_x == 1 && dot_product_dims == 9) {
        return convolve_accumulative(
            out_height,
            out_width,
            1,
            1,
            filter_mat,
            9,
            in);
    }
    if (strides_y == 1 && strides_x == 1 && dot_product_dims == 192) {
        return convolve_accumulative(
            out_height,
            out_width,
            1,
            1,
            filter_mat,
            192,
            in);
    }
    if (strides_y == 1 && strides_x == 1 && dot_product_dims == 384) {
        return convolve_accumulative(
            out_height,
            out_width,
            1,
            1,
            filter_mat,
            384,
            in);
    }
    if (strides_y == 1 && strides_x == 1 && dot_product_dims == 768) {
        return convolve_accumulative(
            out_height,
            out_width,
            1,
            1,
            filter_mat,
            768,
            in);
    }
    if (strides_y == 1 && strides_x == 1 && dot_product_dims == 1536) {
        return convolve_accumulative(
            out_height,
            out_width,
            1,
            1,
            filter_mat,
            1536,
            in);
    }
    return convolve_accumulative(
        out_height,
        out_width,
        strides_y,
        strides_x,
        filter_mat,
        dot_product_dims,
        in);
}

enum class padding { valid, same, causal };

struct convolution_config
{
    std::size_t pad_top_;
    std::size_t pad_bottom_;
    std::size_t pad_left_;
    std::size_t pad_right_;
    std::size_t out_height_;
    std::size_t out_width_;
};

inline convolution_config preprocess_convolution(
    const shape2& filter_shape,
    const shape2& strides,
    padding pad_type,
    std::size_t input_shape_height,
    std::size_t input_shape_width)
{
    // https://www.tensorflow.org/api_guides/python/nn#Convolution
    const int filter_height = static_cast<int>(filter_shape.height_);
    const int filter_width = static_cast<int>(filter_shape.width_);
    const int in_height = static_cast<int>(input_shape_height);
    const int in_width = static_cast<int>(input_shape_width);
    const int strides_y = static_cast<int>(strides.height_);
    const int strides_x = static_cast<int>(strides.width_);

    int out_height = 0;
    int out_width = 0;

    if (pad_type == padding::same || pad_type == padding::causal)
    {
        out_height = fplus::ceil(static_cast<float>(in_height) / static_cast<float>(strides_y) - 0.001);
        out_width  = fplus::ceil(static_cast<float>(in_width) / static_cast<float>(strides_x) - 0.001);
    }
    else
    {
        out_height = fplus::ceil(static_cast<float>(in_height - filter_height + 1) / static_cast<float>(strides_y) - 0.001);
        out_width = fplus::ceil(static_cast<float>(in_width - filter_width + 1) / static_cast<float>(strides_x) - 0.001);
    }

    int pad_top = 0;
    int pad_bottom = 0;
    int pad_left = 0;
    int pad_right = 0;

    if (pad_type == padding::same)
    {
        int pad_along_height = 0;
        int pad_along_width = 0;

        if (in_height % strides_y == 0)
            pad_along_height = std::max(filter_height - strides_y, 0);
        else
            pad_along_height = std::max(filter_height - (in_height % strides_y), 0);
        if (in_width % strides_x == 0)
            pad_along_width = std::max(filter_width - strides_x, 0);
        else
            pad_along_width = std::max(filter_width - (in_width % strides_x), 0);

        pad_top = pad_along_height / 2;
        pad_bottom = pad_along_height - pad_top;
        pad_left = pad_along_width / 2;
        pad_right = pad_along_width - pad_left;
    }
    else if (pad_type == padding::causal)
    {
        pad_top = filter_height - 1;
        pad_left = filter_width - 1;
    }

    std::size_t out_height_size_t = fplus::integral_cast_throw<std::size_t>(out_height);
    std::size_t out_width_size_t = fplus::integral_cast_throw<std::size_t>(out_width);
    std::size_t pad_top_size_t = fplus::integral_cast_throw<std::size_t>(pad_top);
    std::size_t pad_bottom_size_t = fplus::integral_cast_throw<std::size_t>(pad_bottom);
    std::size_t pad_left_size_t = fplus::integral_cast_throw<std::size_t>(pad_left);
    std::size_t pad_right_size_t = fplus::integral_cast_throw<std::size_t>(pad_right);

    return {pad_top_size_t, pad_bottom_size_t,
        pad_left_size_t, pad_right_size_t,
        out_height_size_t, out_width_size_t};
}

inline tensor5 convolve(
    const shape2& strides,
    const padding& pad_type,
    const im2col_filter_matrix& filter_mat,
    const tensor5& input)
{
    assertion(filter_mat.filter_shape_.depth_ == input.shape().depth_,
        "invalid filter depth");

    const auto conv_cfg = preprocess_convolution(
        filter_mat.filter_shape_.without_depth(),
        strides, pad_type, input.shape().height_, input.shape().width_);

    const std::size_t out_height = conv_cfg.out_height_;
    const std::size_t out_width = conv_cfg.out_width_;

    const auto in_padded = pad_tensor5(0,
        conv_cfg.pad_top_, conv_cfg.pad_bottom_, conv_cfg.pad_left_, conv_cfg.pad_right_,
        input);

    const std::size_t dot_product_dims = filter_mat.filter_shape_.depth_ * filter_mat.filter_shape_.width_;

    return convolve_accumulative_fix(
        out_height, out_width,
        strides.height_, strides.width_,
        filter_mat,
        dot_product_dims,
        in_padded);
}

} } // namespace fdeep, namespace internal
