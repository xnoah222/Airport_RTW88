/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef _RTW88_COMPAT_AVERAGE_H
#define _RTW88_COMPAT_AVERAGE_H

#include "types.h"

/* Exponentially Weighted Moving Average (EWMA) */
#define DECLARE_EWMA(name, _precision, _weight_rcp)                       \
    struct ewma_##name {                                                   \
        unsigned long internal;                                            \
    };                                                                     \
    static inline void ewma_##name##_init(struct ewma_##name *e)          \
    {                                                                      \
        e->internal = 0;                                                   \
    }                                                                      \
    static inline unsigned long ewma_##name##_read(struct ewma_##name *e) \
    {                                                                      \
        return e->internal >> (_precision);                                \
    }                                                                      \
    static inline void ewma_##name##_add(struct ewma_##name *e,           \
                                          unsigned long val)               \
    {                                                                      \
        unsigned long internal = e->internal;                             \
        unsigned long weight_rcp = (_weight_rcp);                         \
        if (internal == 0)                                                 \
            e->internal = val << (_precision);                             \
        else                                                               \
            e->internal = (internal - (internal >> weight_rcp)) +         \
                          (val << ((_precision) - weight_rcp));            \
    }

#endif /* _RTW88_COMPAT_AVERAGE_H */
