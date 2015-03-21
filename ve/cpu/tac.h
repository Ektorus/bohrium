#ifndef __BH_VE_CPU_TAC
#define __BH_VE_CPU_TAC
//
//  NOTE: This file is autogenerated based on the tac-definition.
//        You should therefore not edit it manually.
//
#include "stdint.h"

// Bohrium custom types, used of representing
// two inputs in one constant... hopefully we can get
// rid of it... at some point...
typedef struct { uint64_t first, second; } pair_LL; 

#ifndef __BH_BASE
#define __BH_BASE
typedef struct
{
    /// The type of data in the array
    uint64_t       type;

    /// The number of elements in the array
    uint64_t      nelem;

    /// Pointer to the actual data.
    void*   data;
}bh_base;
#endif

typedef enum OPERATION {
    MAP        = 1,
    ZIP        = 2,
    REDUCE     = 4,
    SCAN       = 8,
    GENERATE   = 16,
    SYSTEM     = 32,
    EXTENSION  = 64,
    NOOP       = 128
} OPERATION;

typedef enum OPERATOR {
    ADD             = 1,
    SUBTRACT        = 2,
    ARCTAN2         = 3,
    BITWISE_AND     = 4,
    BITWISE_OR      = 5,
    BITWISE_XOR     = 6,
    DIVIDE          = 7,
    EQUAL           = 8,
    GREATER         = 9,
    GREATER_EQUAL   = 10,
    LEFT_SHIFT      = 11,
    LESS            = 12,
    LESS_EQUAL      = 13,
    LOGICAL_AND     = 14,
    LOGICAL_OR      = 15,
    LOGICAL_XOR     = 16,
    MAXIMUM         = 17,
    MINIMUM         = 18,
    MOD             = 19,
    MULTIPLY        = 20,
    NOT_EQUAL       = 21,
    POWER           = 22,
    RIGHT_SHIFT     = 23,
    ABSOLUTE        = 24,
    ARCCOS          = 25,
    ARCCOSH         = 26,
    ARCSIN          = 27,
    ARCSINH         = 28,
    ARCTAN          = 29,
    ARCTANH         = 30,
    CEIL            = 31,
    COS             = 32,
    COSH            = 33,
    EXP             = 34,
    EXP2            = 35,
    EXPM1           = 36,
    FLOOR           = 37,
    IDENTITY        = 38,
    IMAG            = 39,
    INVERT          = 41,
    ISINF           = 42,
    ISNAN           = 43,
    LOG             = 44,
    LOG10           = 45,
    LOG1P           = 46,
    LOG2            = 47,
    LOGICAL_NOT     = 48,
    REAL            = 49,
    RINT            = 50,
    SIN             = 51,
    SINH            = 52,
    SQRT            = 53,
    TAN             = 54,
    TANH            = 55,
    TRUNC           = 56,
    DISCARD         = 57,
    FREE            = 58,
    SYNC            = 59,
    NONE            = 60,
    FLOOD           = 61,
    RANDOM          = 62,
    RANGE           = 63,
    EXTENSION_OPERATOR = 64
} OPERATOR;

typedef enum ETYPE {
    BOOL       = 1,
    INT8       = 2,
    INT16      = 4,
    INT32      = 8,
    INT64      = 16,
    UINT8      = 32,
    UINT16     = 64,
    UINT32     = 128,
    UINT64     = 256,
    FLOAT32    = 512,
    FLOAT64    = 1024,
    COMPLEX64  = 2048,
    COMPLEX128 = 4096,
    PAIRLL     = 8192
} ETYPE;

typedef enum LAYOUT {
    SCALAR_CONST = 1,
    SCALAR     = 2,
    CONTRACTABLE = 4,
    CONTIGUOUS = 8,
    CONSECUTIVE = 16,
    STRIDED    = 32,
    SPARSE     = 64
} LAYOUT;   // Uses a single byte

typedef struct tac {
    OPERATION op;       // Operation
    OPERATOR  oper;     // Operator
    uint32_t  out;      // Output operand
    uint32_t  in1;      // First input operand
    uint32_t  in2;      // Second input operand
    void* ext;
} tac_t;

typedef struct operand {
    LAYOUT  layout;     // The layout of the data
    void**  data;       // Pointer to pointer that points memory allocated for the array
    void*   const_data; // Pointer to constant
    ETYPE   etype;      // Type of the elements stored
    int64_t start;      // Offset from memory allocation to start of array
    int64_t nelem;      // Number of elements available in the allocation

    int64_t ndim;       // Number of dimensions of the array
    int64_t* shape;     // Shape of the array
    int64_t* stride;    // Stride in each dimension of the array
    bh_base* base;      // Pointer to operand base or NULL when layout == SCALAR_CONST.
} operand_t;            // Meta-data for a block argument

typedef struct iterspace {
    LAYOUT layout;  // The dominating layout
    int64_t ndim;   // The dominating rank/dimension of the iteration space
    int64_t* shape; // Shape of the iteration space
    int64_t nelem;  // The number of elements in the iteration space
} iterspace_t;

#define SCALAR_LAYOUT   ( SCALAR | SCALAR_CONST | CONTRACTABLE )
#define ARRAY_LAYOUT    ( CONTIGUOUS | CONSECUTIVE | STRIDED | SPARSE )
#define COLLAPSIBLE     ( SCALAR | SCALAR_CONST | CONTRACTABLE | CONTIGUOUS | CONSECUTIVE )

#define EWISE           ( MAP | ZIP | GENERATE )
#define ARRAY_OPS       ( MAP | ZIP | GENERATE | REDUCE | SCAN )
#define NBUILTIN_OPS    7
#define NBUILTIN_OPERS  62
#define NON_FUSABLE     ( REDUCE | SCAN | EXTENSION )

#endif
