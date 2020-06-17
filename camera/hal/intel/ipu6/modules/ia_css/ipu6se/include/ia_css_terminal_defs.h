/*
 * Copyright (C) 2020 Intel Corporation.
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

#ifndef __IA_CSS_TERMINAL_DEFS_H
#define __IA_CSS_TERMINAL_DEFS_H

#include "type_support.h"

/**
 * @addtogroup group_psysapi
 * @{
 */

#define IA_CSS_TERMINAL_ID_BITS		8
typedef uint8_t				ia_css_terminal_ID_t;
#define IA_CSS_TERMINAL_INVALID_ID	((ia_css_terminal_ID_t)(-1))

/**
 * Terminal category
 *
 * @todo New for IPU7 POC
 */
typedef enum ia_css_terminal_cat {
	IA_CSS_TERMINAL_CAT_LOAD = 0, /**< Load terminal.  Payload is made of up segmented value blobs for register load. */
	IA_CSS_TERMINAL_CAT_CONNECT, /**< Connect terminal.  Payload is a memory pointer. */
	IA_CSS_TERMINAL_CAT_COUNT /**< Number of entries in this enumeration */
} ia_css_terminal_cat_t;
#define IA_CSS_TERMINAL_CAT_INVALID IA_CSS_TERMINAL_CAT_COUNT

/**
 * Terminal direction
 *
 * Direction is from the IPU perspective.  That is, "IN" is input consumed by IPU and "OUT" is
 * output generated by IPU.
 *
 * @todo New for IPU7 POC
 */
typedef enum ia_css_terminal_dir {
	IA_CSS_TERMINAL_DIR_IN = 0, /**< Input terminal. Input is consumed by IPU*/
	IA_CSS_TERMINAL_DIR_OUT, /**< Output terminal. Output is generated by IPU */
	IA_CSS_TERMINAL_DIR_COUNT /**< Number of entries in this enumeration */
} ia_css_terminal_dir_t;
#define IA_CSS_TERMINAL_DIR_INVALID IA_CSS_TERMINAL_DIR_COUNT

/**
 * Terminal Rate Of Update (ROU)
 *
 * Direction is from the IPU perspective.  That is, "IN" is input consumed by IPU and "OUT" is
 * output generated by IPU.
 *
 * @todo New for IPU7 POC
 */
typedef enum ia_css_terminal_rou {
	IA_CSS_TERMINAL_ROU_STREAM = 0, /**< Constant for all frames in a stream */
	IA_CSS_TERMINAL_ROU_FRAME, /**< Constant for all fragments in a frame */
	IA_CSS_TERMINAL_ROU_FRAG, /**< Requires update (i.e. not constant) for all fragments */
	IA_CSS_TERMINAL_ROU_COUNT /**< Number of entries in this enumeration */
} ia_css_terminal_rou_t;
#define IA_CSS_TERMINAL_ROU_INVALID IA_CSS_TERMINAL_ROU_COUNT

/**
 * Connect terminal payload content type
 *
 * Defines the broad usage of the buffers defined in connect terminals
 */
typedef enum ia_css_connect_buf_type {
	IA_CSS_CONNECT_BUF_TYPE_DATA = 0, /**< Image data */
	IA_CSS_CONNECT_BUF_TYPE_META, /**< Meta data */
	IA_CSS_CONNECT_BUF_TYPE_UNKNOWN, /**< Unknown or irrelevant (e.g. load terminal) */
	IA_CSS_CONNECT_BUF_TYPE_COUNT /**< Number of entries in this enumeration */
} ia_css_connect_buf_type_t;

/** Unknown or irrelevant buffer type */
#define IA_CSS_CONNECT_BUF_TYPE_INVALID IA_CSS_CONNECT_BUF_TYPE_COUNT

/**
 * Terminal type identifier
 *
 * @note  Will be superseded by  ia_css_terminal_cat_t, ia_css_terminal_dir_t, ia_css_terminal_rou_t,
 * and ia_css_connect_buf_type_t
 * To stage the changes, this type will remain - for now...
 *
 * Inital type to attribute mapping table for staging:
 * ia_css_terminal_type_t                    | ia_css_terminal_cat_t | ia_css_terminal_dir_t | ia_css_terminal_rou_t | ia_css_connect_buf_type_t    | notes
 * ----------------------------------------- | --------------------- | --------------------- | --------------------- | ---------------------------- | -----
 * IA_CSS_TERMINAL_TYPE_DATA_IN              | CAT_CONNECT           | DIR_IN                | ROU_FRAG?             | BUF_TYPE_DATA                | Fragment handling unclear
 * IA_CSS_TERMINAL_TYPE_DATA_OUT             | CAT_CONNECT           | DIR_OUT               | ROU_FRAG?             | BUF_TYPE_DATA                | Fragment handling unclear
 * IA_CSS_TERMINAL_TYPE_PARAM_STREAM         | OBSOLETE              | OBSOLETE              | OBSOLETE              | OBSOLETE                     | *No meaningful use found in FW*
 * IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN      | CAT_LOAD              | DIR_IN                | ROU_FRAME             | BUF_TYPE_UNKNOWN (load term) |
 * IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT     | CAT_LOAD              | DIR_OUT               | ROU_FRAME             | BUF_TYPE_UNKNOWN (load term) |
 * IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN     | CAT_CONNECT           | DIR_IN                | ROU_FRAME             | BUF_TYPE_META                |
 * IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT    | CAT_CONNECT           | DIR_OUT               | ROU_FRAME             | BUF_TYPE_META                |
 * IA_CSS_TERMINAL_TYPE_PARAM_SLICED_IN      | CAT_LOAD              | DIR_IN                | ROU_FRAME?            | BUF_TYPE_UNKNOWN (load term) | Usage unclear
 * IA_CSS_TERMINAL_TYPE_PARAM_SLICED_OUT     | CAT_LOAD              | DIR_OUT               | ROU_FRAME?            | BUF_TYPE_UNKNOWN (load term) | Usage unclear
 * IA_CSS_TERMINAL_TYPE_STATE_IN             | OBSOLETE              | OBSOLETE              | OBSOLETE              | OBSOLETE                     | *No meaningful use found in FW*
 * IA_CSS_TERMINAL_TYPE_STATE_OUT            | OBSOLETE              | OBSOLETE              | OBSOLETE              | OBSOLETE                     | *No meaningful use found in FW*
 * IA_CSS_TERMINAL_TYPE_PROGRAM              | CAT_LOAD              | DIR_IN                | ROU_FRAG              | BUF_TYPE_UNKNOWN (load term) |
 * IA_CSS_TERMINAL_TYPE_PROGRAM_CONTROL_INIT | CAT_LOAD - see note   | DIR_IN                | ROU_STREAM - see note | BUF_TYPE_UNKNOWN (load term) | Belongs to FW team.  Used to have both load and connect sections.  Currently calculated based on PPG terminal information, and not buffer set.
 */
typedef enum ia_css_terminal_type {
	/** Data input */
	IA_CSS_TERMINAL_TYPE_DATA_IN = 0,
	/** Data output */
	IA_CSS_TERMINAL_TYPE_DATA_OUT,
	/** Type 6 parameter input */
	IA_CSS_TERMINAL_TYPE_PARAM_STREAM,
	/** Type 1-5 parameter input.  Constant for all fragments in a frame. */
	IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN,
	/** Type 1-5 parameter output */
	IA_CSS_TERMINAL_TYPE_PARAM_CACHED_OUT,
	/** Represent the new type of terminal for the
	 * "spatial dependent parameters", when params go in
	 */
	IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_IN,
	/** Represent the new type of terminal for the
	 * "spatial dependent parameters", when params go out
	 */
	IA_CSS_TERMINAL_TYPE_PARAM_SPATIAL_OUT,
	/** Represent the new type of terminal for the
	 * explicit slicing, when params go in
	 */
	IA_CSS_TERMINAL_TYPE_PARAM_SLICED_IN,
	/** Represent the new type of terminal for the
	 * explicit slicing, when params go out
	 */
	IA_CSS_TERMINAL_TYPE_PARAM_SLICED_OUT,
	/** State (private data) input */
	IA_CSS_TERMINAL_TYPE_STATE_IN,
	/** State (private data) output */
	IA_CSS_TERMINAL_TYPE_STATE_OUT,
	/** Program parameters, may change per fragment */
	IA_CSS_TERMINAL_TYPE_PROGRAM,
	/** Program control parameters.  Non-alogrithmic parameters for system devices. */
	IA_CSS_TERMINAL_TYPE_PROGRAM_CONTROL_INIT,
	IA_CSS_N_TERMINAL_TYPES
} ia_css_terminal_type_t;

#define IA_CSS_TERMINAL_TYPE_BITS				32

/* Temporary redirection needed to facilicate merging with the drivers
   in a backwards compatible manner */
#define IA_CSS_TERMINAL_TYPE_PARAM_CACHED IA_CSS_TERMINAL_TYPE_PARAM_CACHED_IN

/**
 * Dimensions of the data objects. Note that a C-style
 * data order is assumed. Data stored by row.
 */
typedef enum ia_css_dimension {
	/** The number of columns, i.e. the size of the row */
	IA_CSS_COL_DIMENSION = 0,
	/** The number of rows, i.e. the size of the column */
	IA_CSS_ROW_DIMENSION = 1,
	IA_CSS_N_DATA_DIMENSION = 2
} ia_css_dimension_t;

#define IA_CSS_N_COMMAND_COUNT (4)

#ifndef PIPE_GENERATION
/* Don't include these complex enum structures in Genpipe, it can't handle and it does not need them */
/**
 * enum ia_css_isys_link_id. Lists the link IDs used by the FW for On The Fly feature
 */
typedef enum ia_css_isys_link_id {
	IA_CSS_ISYS_LINK_OFFLINE = 0,
	IA_CSS_ISYS_LINK_MAIN_OUTPUT = 1,
	IA_CSS_ISYS_LINK_PDAF_OUTPUT = 2
} ia_css_isys_link_id_t;
#define N_IA_CSS_ISYS_LINK_ID	(IA_CSS_ISYS_LINK_PDAF_OUTPUT + 1)

/**
 * enum ia_css_data_barrier_link_id. Lists the link IDs used by the FW for data barrier feature
 */
typedef enum ia_css_data_barrier_link_id {
	IA_CSS_DATA_BARRIER_LINK_MEMORY_0 = N_IA_CSS_ISYS_LINK_ID,
	IA_CSS_DATA_BARRIER_LINK_MEMORY_1,
	IA_CSS_DATA_BARRIER_LINK_MEMORY_2,
	IA_CSS_DATA_BARRIER_LINK_MEMORY_3,
	IA_CSS_DATA_BARRIER_LINK_MEMORY_4,
	N_IA_CSS_DATA_BARRIER_LINK_ID
} ia_css_data_barrier_link_id_t;

/**
 * enum ia_css_stream2gen_link_id. Lists the link IDs used by the FW for streaming to GEN
 * support.
 */
typedef enum ia_css_stream2gen_link_id {
	IA_CSS_STREAM2GEN_LINK_ID_0 = N_IA_CSS_DATA_BARRIER_LINK_ID,
	IA_CSS_STREAM2GEN_LINK_ID_1,
	IA_CSS_STREAM2GEN_LINK_ID_2,
	IA_CSS_STREAM2GEN_LINK_ID_3,
	N_IA_CSS_STREAM2GEN_LINK_ID
} ia_css_stream2gen_link_id_t;

#endif /* #ifndef PIPE_GENERATION */
/** @} */
#endif /* __IA_CSS_TERMINAL_DEFS_H */
