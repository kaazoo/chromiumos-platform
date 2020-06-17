/*
 * Copyright (C) 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*! \file ia_abstraction.h
   \brief Constants, definitions and macros used IA modules.
*/
#ifndef _IA_ABSTRACTION_H_
#define _IA_ABSTRACTION_H_

#include <string.h>  /* defines memcpy and memset */
#include <stdlib.h>  /* defines malloc and free */
#include <stddef.h>  /* defines NULL */
#include <stdint.h>  /* defines fixed width integers */
#include <stdio.h>   /* defines sprintf_s */
#include <assert.h>
#include <math.h>


/*!
 * \brief extra Q number format typedefs.
 */
typedef int16_t sq7_8_t;
typedef uint16_t uq8_8_t;
typedef uint16_t uq6_10_t;
typedef uint16_t uq4_12_t;
typedef int32_t sq15_16_t;
typedef uint32_t uq16_16_t;
typedef uint16_t half;
/* MISRA typedefs*/
typedef float float32_t;
typedef double float64_t;

/* Float Epsilon for divide by zero checks */
#define IA_EPSILON 0.0001f

#define FLOAT_TO_Q16_16(n) (uint32_t)(((float)(n))*65536.0f)
#define INT_TO_Q16_16(n)   ((n)<<16)
#define Q16_16_TO_FLOAT(n) (((float)(n))*0.0000152587890625f)
#define Q16_16_TO_INT(n)   ((n)>>16)

#define FLOAT_TO_Q1_15(n)  (uint16_t)(((float)(n))*32768.0f)
#define Q1_15_TO_FLOAT(n)  (((float)(n))*0.000030518f)
#define QX_15_TO_FLOAT(n)  (((float)(n))*0.000030517578125f)

#define FLOAT_TO_Q8_8(n)   (uint16_t)(((float)(n))*256.0f)
#define INT_TO_Q8_8(n)     ((n)<<8)
#define Q8_8_TO_FLOAT(n)   (((float)(n))*0.00390625f)
#define Q8_8_TO_INT(n)     ((n)>>8)

#define FLOAT_TO_QX_3(n)   ((float)(n)*8.0f)
#define FLOAT_TO_QX_7(n)   ((float)(n)*128.0f)
#define FLOAT_TO_QX_8(n)   ((float)(n)*256.0f)
#define FLOAT_TO_QX_10(n)  ((n)*1024.0f)
#define FLOAT_TO_QX_11(n)  ((float)(n)*2048.0f)
#define FLOAT_TO_QX_12(n)  ((float)(n)*4096.0f)
#define FLOAT_TO_QX_13(n)  ((float)(n)*8192.0f)
#define FLOAT_TO_QX_14(n)  ((float)(n)*16384.0f)
#define FLOAT_TO_QX_15(n)  ((float)(n)*32768.0f)
#define INT_TO_QX_10(n)    ((n)<<10)
#define QX_7_TO_FLOAT(n)   (((float)(n))*0.0078125f)
#define QX_10_TO_FLOAT(n)  (((float)(n))*0.0009765625f)
#define QX_13_TO_FLOAT(n)  (((float)(n))*0.0001220703125f)
#define QX_14_TO_FLOAT(n)  (((float)(n))*0.00006103515625f)
#define QX_18_TO_FLOAT(n)  (((float)(n))*0.00000381469f)
#define QX_20_TO_FLOAT(n)  (((float)(n))*0.00000095367431640625f)
#define QX_10_TO_INT(n)    ((n)>>10)

#define Q16_12_TO_FLOAT(n) (((float)(n))*0.000244141f)


/*!
 * \brief Calculates aligned value.
 * Works only with unsigned values.
 * \param a Number to align.
 * \param b Alignment.
 * \return  Aligned number.
 */
#define IA_ALIGN(a,b)            (((unsigned)(a)+(unsigned)(b-1)) & ~(unsigned)(b-1))

#define IA_ALLOC(x)              malloc(x)
#define IA_CALLOC(x)             calloc(1, x)
#define IA_REALLOC(x, y)         realloc(x, y)
#define IA_FREEZ(x)              { free(x); x = NULL;}
#define IA_MEMSET(_Dst, _Val, _Size)       ((void) memset(_Dst, _Val, _Size))
#define IA_MEMCOMPARE(_Buf1,_Buf2,_Size)     memcmp(_Buf1, _Buf2, _Size)
#define IA_ABS(a)                abs((int)(a))
#define IA_FABS(a)               fabsf((float)(a))
#define IA_FABSD(a)              fabs(a)
#define IA_MIN(a, b)             ((a) < (b) ? (a) : (b))
#define IA_MAX(a, b)             ((a) > (b) ? (a) : (b))
#define IA_LIMIT(val, min, max)  IA_MIN(IA_MAX(val, min), max)
#define IA_POW(a, b)             powf((float)(a), (float)(b))
#define IA_POWD(a, b)            pow(a, b)
#define IA_EXP(a)                expf((float)(a))
#define IA_EXPD(a)               exp(a)
#define IA_SQRT(a)               sqrtf((float)(a))
#define IA_SQRTD(a)              sqrt(a)
#define IA_HYPOT(x,y)            hypotf((float)(x),(float)(y))
#define IA_ROUND(a)              (((float)(a) > 0.0F) ? floorf((float)(a) + 0.5F) : ceilf((float)(a) - 0.5F))
#define IA_ROUNDD(a)             (((double)(a) > 0.0) ? floor((double)(a) + 0.5) : ceil((double)(a) - 0.5))

#define IA_CEIL(a)               ceilf((float)(a))
#define IA_CEILD(a)              ceil(a)
#define IA_FLOOR(a)              floorf((float)(a))
#define IA_FLOORD(a)             floor(a)
#define IA_SIN(a)                sinf((float)(a))
#define IA_COS(a)                cosf((float)(a))
#define IA_ATAN(a)               atanf((float)(a))
#define IA_LN(a)                 logf((float)(a))
#define IA_UNUSED(x)             (void)x
#define IA_LOG2(x)               (logf((float)(x)) / logf(2.0f))
#define IA_LOG2D(x)              (log(x) / log(2.0))
#define IA_LOG10(x)              log10f((float)(x))
#define IA_ASSERT                assert
#define IA_SIGN(a)               (((a) > 0) - ((a) < 0))



#define IA_MAX_FIXEDPOINT(integer_bits, frac_bits) ((double)((integer_bits?(2<<(integer_bits-1)):1)) - (1.0f/((double)(frac_bits?((unsigned long)2<<(frac_bits-1)):0))))
#define IA_MIN_FIXEDPOINT(integer_bits, frac_bits) (-IA_MAX_FIXEDPOINT((integer_bits), frac_bits))
#define IA_MAX_Q0_FIXEDPOINT(frac_bits) (1.0 - (1.0f/((double)(frac_bits?((unsigned long)2<<(frac_bits-1)):0))))

/* Q0_31 means: total 31 bits =  0 int bits + 31 fractional bits*/
#define IA_QX_31_FRAC_BITS  (31)
#define IA_Q0_31_MIN    (0)
#define IA_Q0_31_MAX    IA_MAX_Q0_FIXEDPOINT(IA_QX_31_FRAC_BITS)
#define IA_FLOAT_TO_Q0_31(val) (uint32_t)(IA_ROUNDD((IA_LIMIT(val, IA_Q0_31_MIN, IA_Q0_31_MAX))*((unsigned long)2<<(IA_QX_31_FRAC_BITS-1))))

#define IA_QX_26_FRAC_BITS  (26)
#define IA_Q0_26_MIN    (0)
#define IA_Q0_26_MAX    IA_MAX_Q0_FIXEDPOINT(IA_QX_26_FRAC_BITS)
#define IA_FLOAT_TO_Q0_26(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_Q0_26_MIN, IA_Q0_26_MAX)*((unsigned long)2<<(IA_QX_26_FRAC_BITS-1)))))

#define IA_QX_16_FRAC_BITS  (16)
#define IA_Q14_16_MIN    (0)
#define IA_Q14_16_MAX    IA_MAX_FIXEDPOINT(14, IA_QX_16_FRAC_BITS)
#define IA_FLOAT_TO_Q14_16(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_Q14_16_MIN, IA_Q14_16_MAX)*((unsigned long)2<<(IA_QX_16_FRAC_BITS-1)))))

#define IA_QX_5_FRAC_BITS  (5)
#define IA_Q14_5_MIN       (0)
#define IA_Q14_5_MAX    IA_MAX_FIXEDPOINT(14, IA_QX_5_FRAC_BITS)
#define IA_FLOAT_TO_Q14_5(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_Q14_5_MIN, IA_Q14_5_MAX)*((unsigned long)2<<(IA_QX_5_FRAC_BITS-1)))))

#define IA_Q3_16_MIN    (0)
#define IA_Q3_16_MAX    IA_MAX_FIXEDPOINT(3, IA_QX_16_FRAC_BITS)
#define IA_FLOAT_TO_Q3_16(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_Q3_16_MIN, IA_Q3_16_MAX)*((unsigned long)2<<(IA_QX_16_FRAC_BITS-1)))))

/* S4.15 means: total 20 bits =  1 sign bit + 4 int bits + 15 fractional bits*/
#define IA_SX_15_FRAC_BITS  (15)
#define IA_S4_15_MIN IA_MIN_FIXEDPOINT(4, IA_SX_15_FRAC_BITS)
#define IA_S4_15_MAX IA_MAX_FIXEDPOINT(4, IA_SX_15_FRAC_BITS)
#define IA_FLOAT_TO_S4_15(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_S4_15_MIN, IA_S4_15_MAX))*((unsigned long)2<<(IA_SX_15_FRAC_BITS-1))))

/* S4.14 means: total 20 bits =  1 sign bit + 4 int bits + 14 fractional bits*/
#define IA_SX_14_FRAC_BITS  (14)
#define IA_S4_14_MIN IA_MIN_FIXEDPOINT(4, IA_SX_14_FRAC_BITS)
#define IA_S4_14_MAX IA_MAX_FIXEDPOINT(4, IA_SX_14_FRAC_BITS)
#define IA_FLOAT_TO_S4_14(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_S4_14_MIN, IA_S4_14_MAX))*((unsigned long)2<<(IA_SX_14_FRAC_BITS-1))))

/* S4.19 means: =  1 sign bit + 4 int bits + 19 fractional bits*/
#define IA_SX_19_FRAC_BITS  (19)
#define IA_S4_19_MIN IA_MIN_FIXEDPOINT(4, IA_SX_19_FRAC_BITS)
#define IA_S4_19_MAX IA_MAX_FIXEDPOINT(4, IA_SX_19_FRAC_BITS)
#define IA_FLOAT_TO_S4_19(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_S4_19_MIN, IA_S4_19_MAX))*((unsigned long)2<<(IA_SX_19_FRAC_BITS-1))))

#define IA_SX_20_FRAC_BITS  (20)
#define IA_S1_20_MIN IA_MIN_FIXEDPOINT(1, IA_SX_20_FRAC_BITS)
#define IA_S1_20_MAX IA_MAX_FIXEDPOINT(1, IA_SX_20_FRAC_BITS)
#define IA_FLOAT_TO_S1_20(val) (uint32_t)(IA_ROUND((IA_LIMIT(val, IA_S1_20_MIN, IA_S1_20_MAX))*((unsigned long)2<<(IA_SX_20_FRAC_BITS-1))))

#define IA_S4_20_MIN IA_MIN_FIXEDPOINT(4, IA_SX_20_FRAC_BITS)
#define IA_S4_20_MAX IA_MAX_FIXEDPOINT(4, IA_SX_20_FRAC_BITS)
#define IA_FLOAT_TO_S4_20(val) (uint32_t)(IA_ROUNDD((IA_LIMIT(val, IA_S4_20_MIN, IA_S4_20_MAX))*((unsigned long)2<<(IA_SX_20_FRAC_BITS-1))))

#define IA_SX_8_FRAC_BITS  (8)
#define IA_S14_8_MIN IA_MIN_FIXEDPOINT(14, IA_SX_8_FRAC_BITS)
#define IA_S14_8_MAX IA_MAX_FIXEDPOINT(14, IA_SX_8_FRAC_BITS)
#define IA_FLOAT_TO_S14_8(val) (uint32_t)(IA_ROUNDD((IA_LIMIT(val, IA_S14_8_MIN, IA_S14_8_MAX))*((unsigned long)2<<(IA_SX_8_FRAC_BITS-1))))

#define IA_S18_8_MIN IA_MIN_FIXEDPOINT(18, IA_SX_8_FRAC_BITS)
#define IA_S18_8_MAX IA_MAX_FIXEDPOINT(18, IA_SX_8_FRAC_BITS)
#define IA_FLOAT_TO_S18_8(val) (uint32_t)(IA_ROUNDD((IA_LIMIT(val, IA_S18_8_MIN, IA_S18_8_MAX))*((unsigned long)2<<(IA_SX_8_FRAC_BITS-1))))

#if ((!defined _WIN32) && (!defined WIN32) && (!defined _WINDOWS) && (!defined WINDOWS) && (!defined __STDC_LIB_EXT1__) && (!defined memcpy_s))

#include <errno.h>
#include "ia_log.h"

#define SAFE_MEM_SUCCESS 0

#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif  /* _ERRNO_T_DEFINED */

inline static errno_t memcpy_s(void *dest, size_t destsz, const void *src, size_t count)
{
    if (NULL == dest) {
        IA_LOG(ia_log_error, "memcpy_s: nullptr received\n");
        return EINVAL;
    }
    errno_t ret = SAFE_MEM_SUCCESS;
    if (count > destsz) {
        IA_LOG(ia_log_error, "memcpy_s: count(%zu) > destsz(%zu), downsizing count to destsz\n", count, destsz);
        count = destsz;
        ret = ERANGE;
    }
    if (src == NULL) {
        memset(dest, 0, count);
        ret = EINVAL;
    }
    else {
        memcpy(dest, src, count);
    }

    return ret;
}

#elif defined (BUILD_FOR_ARM)
#include "ia_log.h"
inline static int memcpy_s(void *dest, size_t destsz, const void *src, size_t count)
{
    int ret = 0;
    if (NULL == dest) {
        IA_LOG(ia_log_error, "memcpy_s: nullptr received\n");
        return -1;
    }
    if (count > destsz) {
        IA_LOG(ia_log_error, "memcpy_s: count(%zu) > destsz(%zu), downsizing count to destsz\n", count, destsz);
        count = destsz;
        ret = -1;
    }
    if (src == NULL) {
        memset(dest, 0, count);
        ret = 0;
    }
    else {
        memcpy(dest, src, count);
    }

    return 0;
}
#endif

#define IA_MEMCOPY(_Dst, _Src, _MaxCount)      memcpy_s(_Dst, _MaxCount, _Src, _MaxCount)
#define IA_MEMCOPYS(_Dst, _DstSize, _Src, _MaxCount) ((void) memcpy_s(_Dst, _DstSize, _Src, _MaxCount))

#if (defined(__STDC_LIB_EXT1__) || defined(_WIN32) || defined(WIN32) || defined(memmove_s))
#define IA_MEMMOVE(_Dst, _Src, _MaxCount)      memmove_s(_Dst, _MaxCount, _Src, _MaxCount)
#define IA_MEMMOVES(_Dst, _DstSize, _Src, _MaxCount) memmove_s(_Dst, _DstSize, _Src, _MaxCount)
#else
#define IA_MEMMOVE(_Dst, _Src, _Size)      memmove(_Dst, _Src, _Size)
#define IA_MEMMOVES(_Dst, _DstSize, _Src, _MaxCount) { IA_ASSERT((size_t)(_MaxCount) <= (size_t)(_DstSize)); memmove(_Dst, _Src, IA_MIN((size_t)(_DstSize), (size_t)(_MaxCount))); }
#endif


#if (defined(__STDC_LIB_EXT1__) || defined(_WIN32) || defined(WIN32) || defined(strnlen_s))
#define IA_STRNLENS(_Str, _MaxCount)      strnlen_s(_Str, _MaxCount)
#else
#define IA_STRNLENS(_Str, _MaxCount)      strlen(_Str)
#endif

#if (defined(__STDC_LIB_EXT1__) || defined(_WIN32) || defined(WIN32) || defined(sprintf_s))
#define IA_SPRINTFS(x,y,z,...)      sprintf_s(x, y, z, ##__VA_ARGS__)
#else
#define IA_SPRINTFS(x,y,z,...)      sprintf(x, z, ##__VA_ARGS__)
#endif

#if ((defined(_WIN32) || defined(WIN32)) && defined(_MSC_VER))
#ifndef BUILD_FOR_ARM
#include <float.h>
#endif
#define IA_ISNAN(val) _isnan((double)(val))
#else
#define IA_ISNAN(val) isnan((double)(val))
#endif

#if ((defined(_WIN32) || defined(WIN32)) && !defined (BUILD_FOR_ARM))

/* P2P_WINDOWS_KERNELSPACE */
/* To be fixed properly. */
  #ifndef P2P_WINDOWS_KERNELSPACE
  #include <windows.h>
  #endif

  #if defined(_MSC_VER)
    #if _MSC_VER>=1900
      #include <stdio.h>   /* defines snprintf */
    #endif
    #if _MSC_VER<1900 && !defined(P2P_WINDOWS_KERNELSPACE)
      #ifndef snprintf
      #define snprintf _snprintf
      #endif
    #endif
    #if !defined(__BOOL_DEFINED)
      #if _MSC_VER >= 1800 /* stdbool.h is available starting from VS2013. */
        #include <stdbool.h>
      #else /* Fallback for older VS versions. */
        typedef unsigned char bool;
        #define true 1
        #define false 0
      #endif
    #endif
  #else
    #include <stdbool.h> /* defines bool */
  #endif

/* P2P_WINDOWS_KERNELSPACE */
/* To be fixed properly. */
  #ifndef P2P_WINDOWS_KERNELSPACE
    typedef HANDLE mutex_t;
    typedef SRWLOCK rwlock_t;
  #endif

  #define IA_MUTEX_CREATE(m)       (m) = CreateMutex(NULL, false, NULL)
  #define IA_MUTEX_DELETE(m)       CloseHandle(m)
  #define IA_MUTEX_LOCK(m)         WaitForSingleObject(m, INFINITE)
  #define IA_MUTEX_UNLOCK(m)       (ReleaseMutex(m) != 0) ? IA_ASSERT(true) : ((void)0)
  #define IA_RWLOCK_CREATE(l)      InitializeSRWLock(&l)
  #define IA_RWLOCK_DELETE(l)      ((void)0)
  #define IA_RWLOCK_WRLOCK(l)      AcquireSRWLockExclusive(&l)
  #define IA_RWLOCK_WRUNLOCK(l)    ReleaseSRWLockExclusive(&l)
  #define IA_RWLOCK_RDLOCK(l)      AcquireSRWLockShared(&l)
  #define IA_RWLOCK_RDUNLOCK(l)    ReleaseSRWLockShared(&l)

/* Use VS-specific headers for SSE vector intrinsics */
  #include <intrin.h>
  #define ALIGNED_MALLOC(size, align) _aligned_malloc(size, align)
  #define ALIGNED_FREE _aligned_free
  #define ALIGNED_TYPE(x, ALIGNMENT) __declspec(align(ALIGNMENT)) x

#elif defined (BUILD_FOR_ARM)
#define IA_MUTEX_CREATE(m)          #error "not supported on ARM"
#define IA_MUTEX_DELETE(m)          #error "not supported on ARM"
#define IA_MUTEX_LOCK(m)            #error "not supported on ARM"
#define IA_MUTEX_UNLOCK(m)          #error "not supported on ARM"
#define IA_RWLOCK_CREATE(l)         #error "not supported on ARM"
#define IA_RWLOCK_DELETE(l)         #error "not supported on ARM"
#define IA_RWLOCK_WRLOCK(l)         #error "not supported on ARM"
#define IA_RWLOCK_WRUNLOCK(l)       #error "not supported on ARM"
#define IA_RWLOCK_RDLOCK(l)         #error "not supported on ARM"
#define IA_RWLOCK_RDUNLOCK(l)       #error "not supported on ARM"

/* Use VS-specific headers for SSE vector intrinsics */
#define ALIGNED_MALLOC(size, align) #error "not supported on ARM"
#define ALIGNED_FREE                #error "not supported on ARM"
#define ALIGNED_TYPE(x, ALIGNMENT)  #error "not supported on ARM"
#else

  #include <stdbool.h> /* defines bool */

  #ifdef __BUILD_FOR_GSD_AOH__
    typedef char mutex_t;
    typedef char rwlock_t;
    #define IA_MUTEX_CREATE(m)
    #define IA_MUTEX_DELETE(m)
    #define IA_MUTEX_LOCK(m)
    #define IA_MUTEX_UNLOCK(m)
    #define IA_RWLOCK_CREATE(l)
    #define IA_RWLOCK_DELETE(l)
    #define IA_RWLOCK_WRLOCK(l)
    #define IA_RWLOCK_WRUNLOCK(l)
    #define IA_RWLOCK_RDLOCK(l)
    #define IA_RWLOCK_RDUNLOCK(l)

    #undef IA_SIN
    #define IA_SIN(a)                dsp_sin_f32((float)(a))
  #else

    #include <pthread.h> /* defined POSIX thread model */
    typedef pthread_mutex_t mutex_t;

    #define IA_MUTEX_CREATE(m)       (pthread_mutex_init(&m, NULL) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_MUTEX_DELETE(m)       (pthread_mutex_destroy(&m) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_MUTEX_LOCK(m)         (pthread_mutex_lock(&m) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_MUTEX_UNLOCK(m)       (pthread_mutex_unlock(&m) == 0) ? IA_ASSERT(true) : ((void)0)

#ifndef ENABLE_CUSTOMIZED_STD_LIB
    typedef pthread_rwlock_t rwlock_t;
    #define IA_RWLOCK_CREATE(l)      (pthread_rwlock_init(&l, NULL) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_DELETE(l)      (pthread_rwlock_destroy(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_WRLOCK(l)      (pthread_rwlock_wrlock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_WRUNLOCK(l)    (pthread_rwlock_unlock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_RDLOCK(l)      (pthread_rwlock_rdlock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_RDUNLOCK(l)    (pthread_rwlock_unlock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
#else
    typedef pthread_mutex_t rwlock_t;
    #define IA_RWLOCK_CREATE(l)      (pthread_mutex_init(&l, NULL) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_DELETE(l)      (pthread_mutex_destroy(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_WRLOCK(l)      (pthread_mutex_lock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_WRUNLOCK(l)    (pthread_mutex_unlock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_RDLOCK(l)      (pthread_mutex_lock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
    #define IA_RWLOCK_RDUNLOCK(l)    (pthread_mutex_unlock(&l) == 0) ? IA_ASSERT(true) : ((void)0)
#endif

/* Use GNU-specific headers for SSE vector intrinsics */
    #if defined __i386__ || defined __x86_64__
      #include <x86intrin.h>
      #include <malloc.h>
      #define ALIGNED_MALLOC(size, align) memalign(align, size)
      #define ALIGNED_FREE free
      #define ALIGNED_TYPE(x, ALIGNMENT) x __attribute__((aligned(ALIGNMENT)))
    #endif
  #endif
#endif

/* These macros are used for allocating one big chunk of memory and assigning parts of it.
* MEMDEBUG flag can be used to debug / check with if memory read & writes stay within the
* boundaries by allocating each memory block individually from system memory. */
#ifdef MEMDEBUG
#define IA_MEMASSIGN(ptr, size)  IA_CALLOC(size); IA_UNUSED(ptr)
#else
#define IA_MEMASSIGN(ptr, size)  ptr; ptr += IA_ALIGN(size, 8)
#endif

#ifndef __cplusplus
#if (defined(_WIN32) || defined(WIN32)) && !defined(__GNUC__)
#define inline __inline
#elif defined(__GNUC__)
#define inline  __inline__
#else
#define inline                    /* default is to define inline to empty */
#endif
#endif

#define ROUND_DOWN(input_size, step_size) ((input_size) & ~((step_size)-1))
#define STEP_SIZE_4 4
#define STEP_SIZE_2 2

#if defined(__ANDROID__)
    #define FILE_DEBUG_DUMP_PATH "/data/misc/cameraserver/"
#elif defined(_WIN32)
    #define FILE_DEBUG_DUMP_PATH "c:\\tmp\\"
#else /* Linux */
    #define FILE_DEBUG_DUMP_PATH "/tmp/"
#endif


#endif /* _IA_ABSTRACTION_H_ */
