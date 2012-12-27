/**
 * @file mat_reduce_internal.h
 *
 * Internal implementation of matrix reduction
 * 
 * @author Dahua Lin 
 */

#ifdef _MSC_VER
#pragma once
#endif

#ifndef LIGHTMAT_MAT_REDUCE_INTERNAL_H_
#define LIGHTMAT_MAT_REDUCE_INTERNAL_H_

#include <light_mat/mateval/mat_fold.h>
#include <light_mat/mateval/common_kernels.h>
#include <light_mat/math/math_functors.h>
#include <light_mat/mateval/mat_arith.h>
#include <light_mat/mateval/mat_emath.h>

namespace lmat { namespace internal {


	template<class RFun>
	struct _fold_kernel
	{
		typedef typename RFun::value_type value_type;
		RFun rfun;

		LMAT_ENSURE_INLINE
		_fold_kernel(const RFun& rf) : rfun(rf) { }

		LMAT_ENSURE_INLINE
		void operator() (value_type& a, const value_type& x) const
		{
			rfun.fold(a, x);
		}

		template<typename Kind>
		LMAT_ENSURE_INLINE
		void operator() (math::simd_pack<value_type, Kind>& a,
				const math::simd_pack<value_type, Kind>& x) const
		{
			rfun.fold(a, x);
		}
	};


	template<class RFun, class TFun>
	struct _foldx_kernel
	{
		typedef typename RFun::value_type value_type;
		RFun rfun;
		TFun tfun;

		LMAT_ENSURE_INLINE
		_foldx_kernel(const RFun& rf, const TFun& tf)
		: rfun(rf), tfun(tf) { }

		template<typename... A>
		LMAT_ENSURE_INLINE
		void operator() (value_type& a, const A&... x) const
		{
			rfun.fold(a, tfun(x...));
		}

		template<typename Kind, typename... A>
		LMAT_ENSURE_INLINE
		void operator() (math::simd_pack<value_type, Kind>& a,
				const math::simd_pack<A, Kind>&... x) const
		{
			rfun.fold(a, tfun(x...));
		}
	};


	/********************************************
	 *
	 *  helpers on atag
	 *
	 ********************************************/

	template<class Folder, class TExpr>
	struct full_reduc_policy
	{
		static_assert(prefers_linear<TExpr>::value,
				"TExpr should allow linear access.");

		static const bool use_linear = prefers_linear<TExpr>::value;

		typedef typename matrix_traits<TExpr>::value_type vtype;
		typedef default_simd_kind simd_kind;

		static const bool use_simd =
				folder_supports_simd<Folder>::value &&
				prefers_simd<TExpr, vtype, simd_kind, use_linear>::value;

		typedef typename meta::if_c<use_simd,
				atags::simd<simd_kind>,
				atags::scalar>::type atag;
	};


	/********************************************
	 *
	 *  helpers on shape and empty value
	 *
	 ********************************************/

	template<typename T, class Mat>
	LMAT_ENSURE_INLINE
	inline index_t reduc_get_length(const IEWiseMatrix<Mat, T>& mat)
	{
		return mat.nelems();
	}

	template<typename T, class Mat1, class Mat2>
	LMAT_ENSURE_INLINE
	inline index_t reduc_get_length(const IEWiseMatrix<Mat1, T>& mat1, const IEWiseMatrix<Mat2, T>& mat2)
	{
		return common_shape(mat1.derived(), mat2.derived()).nelems();
	}


	template<typename T, class Mat>
	LMAT_ENSURE_INLINE
	inline typename meta::shape<Mat>::type
	reduc_get_shape(const IEWiseMatrix<Mat, T>& mat)
	{
		return mat.shape();
	}

	template<typename T, class Mat1, class Mat2>
	LMAT_ENSURE_INLINE
	inline typename meta::common_shape<Mat1, Mat2>::type
	reduc_get_shape(const IEWiseMatrix<Mat1, T>& mat1, const IEWiseMatrix<Mat2, T>& mat2)
	{
		return common_shape(mat1.derived(), mat2.derived());
	}

	template<typename T>
	struct empty_values
	{
		LMAT_ENSURE_INLINE
		static T sum() { return T(0); }

		LMAT_ENSURE_INLINE
		static T mean() { return std::numeric_limits<T>::quiet_NaN(); }

		LMAT_ENSURE_INLINE
		static T maximum() { return - std::numeric_limits<T>::infinity(); }

		LMAT_ENSURE_INLINE
		static T minimum() { return std::numeric_limits<T>::infinity(); }
	};



	/********************************************
	 *
	 *  colwise reduction
	 *
	 ********************************************/

	template<int M, int N, typename U, class Folder, class DMat, class MultiColReader>
	inline void colwise_fold_impl(const matrix_shape<M, N>& shape, U u,
			const Folder& folder, DMat& dmat, const MultiColReader& rd)
	{
		dimension<M> col_dim(shape.nrows());
		const index_t n = shape.ncolumns();

		vecfold_kernel<Folder, U> fker = fold(folder, u);

		for (index_t j = 0; j < n; ++j)
		{
			dmat[j] = fker.apply(col_dim, rd.col(j));
		}
	}

	template<int M, int N, typename U, class Folder, class DMat, typename TFun, typename... MultiColReader>
	inline void colwise_foldx_impl(const matrix_shape<M, N>& shape, U u,
			const Folder& folder, DMat& dmat, const TFun& tfun, const MultiColReader&... rds)
	{
		dimension<M> col_dim(shape.nrows());
		const index_t n = shape.ncolumns();

		vecfoldf_kernel<Folder, TFun, U> fker = foldf(folder, tfun, u);

		for (index_t j = 0; j < n; ++j)
		{
			dmat[j] = fker.apply(col_dim, rds.col(j)...);
		}
	}


	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void colwise_sum_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		colwise_fold_impl(shape, u, sum_folder<T>(), dmat, make_multicol_accessor(u, wrap));
	}

	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void colwise_mean_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		const index_t m = shape.nrows();

		colwise_fold_impl(shape, u, sum_folder<T>(), dmat, make_multicol_accessor(u, wrap));
		dmat *= math::rcp((T)(m));
	}

	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void colwise_maximum_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		colwise_fold_impl(shape, u, maximum_folder<T>(), dmat, make_multicol_accessor(u, wrap));
	}

	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void colwise_minimum_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		colwise_fold_impl(shape, u, minimum_folder<T>(), dmat, make_multicol_accessor(u, wrap));
	}


	template<int M, int N, typename Kind, class DMat, typename TFun, typename... Wrap>
	inline void colwise_sumx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		colwise_foldx_impl(shape, u, sum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);
	}

	template<int M, int N, typename Kind, class DMat, typename TFun, typename... Wrap>
	inline void colwise_meanx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		const index_t m = shape.nrows();

		colwise_foldx_impl(shape, u, sum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);
		dmat *= math::rcp((T)m);
	}

	template<int M, int N, typename Kind, class DMat, typename TFun, typename... Wrap>
	inline void colwise_maximumx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		colwise_foldx_impl(shape, u, maximum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);
	}

	template<int M, int N, typename Kind, class DMat, typename TFun, typename... Wrap>
	inline void colwise_minimumx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		colwise_foldx_impl(shape, u, minimum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);
	}


	/********************************************
	 *
	 *  rowwise reduction
	 *
	 ********************************************/

	template<int M, int N, typename U, class RFun, class DMat, class MultiColReader>
	inline void rowwise_fold_impl(const matrix_shape<M, N>& shape, U u,
			RFun rfun, DMat& dmat, const MultiColReader& rd)
	{
		dimension<M> col_dim(shape.nrows());
		const index_t n = shape.ncolumns();
		typedef typename matrix_traits<DMat>::value_type T;

		auto a = make_vec_accessor(u, in_out_(dmat));

		internal::linear_ewise_eval(col_dim, u, copy_kernel<T>(), rd.col(0), a);

		_fold_kernel<RFun> fker(rfun);
		for (index_t j = 1; j < n; ++j)
		{
			internal::linear_ewise_eval(col_dim, u, fker, a, rd.col(j));
		}
	}

	template<int M, int N, typename U, class RFun, class DMat, typename TFun, typename... MultiColReader>
	inline void rowwise_foldx_impl(const matrix_shape<M, N>& shape, U u,
			RFun rfun, DMat& dmat, const TFun& tfun, const MultiColReader&... rds)
	{
		dimension<M> col_dim(shape.nrows());
		const index_t n = shape.ncolumns();

		auto a = make_vec_accessor(u, in_out_(dmat));

		internal::linear_ewise_eval(col_dim, u, map_kernel<TFun>(tfun), a, rds.col(0)...);

		_foldx_kernel<RFun, TFun> fker(rfun, tfun);
		for (index_t j = 1; j < n; ++j)
		{
			internal::linear_ewise_eval(col_dim, u, fker, a, rds.col(j)...);
		}
	}


	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void rowwise_sum_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_fold_impl(shape, u, sum_folder<T>(), dmat, make_multicol_accessor(u, wrap));
	}

	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void rowwise_mean_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_fold_impl(shape, u, sum_folder<T>(), dmat, make_multicol_accessor(u, wrap));

		T c = T(1) / T(shape.ncolumns());
		dimension<M> dim(shape.nrows());
		map(math::mul_fun<T>(), atags::simd<Kind>())(dim, out_(dmat), in_(dmat), in_(c, atags::single()));
	}

	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void rowwise_maximum_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_fold_impl(shape, u, maximum_folder<T>(), dmat, make_multicol_accessor(u, wrap));
	}

	template<int M, int N, typename Kind, class DMat, class Wrap>
	inline void rowwise_minimum_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat, const Wrap& wrap)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_fold_impl(shape, u, minimum_folder<T>(), dmat, make_multicol_accessor(u, wrap));
	}


	template<int M, int N, typename Kind, class DMat, class TFun, typename... Wrap>
	inline void rowwise_sumx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_foldx_impl(shape, u, sum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);
	}

	template<int M, int N, typename Kind, class DMat, class TFun, typename... Wrap>
	inline void rowwise_meanx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_foldx_impl(shape, u, sum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);

		T c = T(1) / T(shape.ncolumns());
		dimension<M> dim(shape.nrows());
		map(math::mul_fun<T>(), atags::simd<Kind>())(dim, out_(dmat), in_(dmat), in_(c, atags::single()));
	}

	template<int M, int N, typename Kind, class DMat, class TFun, typename... Wrap>
	inline void rowwise_maximumx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_foldx_impl(shape, u, maximum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);
	}

	template<int M, int N, typename Kind, class DMat, class TFun, typename... Wrap>
	inline void rowwise_minimumx_(const matrix_shape<M, N>& shape, atags::simd<Kind> u, DMat& dmat,
			const TFun& tfun, const Wrap&... wraps)
	{
		typedef typename matrix_traits<DMat>::value_type T;
		rowwise_foldx_impl(shape, u, minimum_folder<T>(), dmat, tfun, make_multicol_accessor(u, wraps)...);
	}

} }

#endif 
