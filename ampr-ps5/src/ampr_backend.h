/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  void *context;
  int (*cancelled)(void *context);
  void (*phase)(void *context,const char *phase);
  void (*progress)(void *context,uint64_t done,uint64_t total);
  void (*log)(void *context,const char *line);
} ampr_backend_callbacks;
/* One operation at a time. Mounted root must already be read-only and stable.
   Index must exist; this entry point never edits the source or mount. */
int ampr_backend_pack(const char *mounted_root,const char *source_index,const char *new_output,int level,const ampr_backend_callbacks *callbacks,char *error,size_t error_size);
/* Production One Shot path: builds a fresh index outside source, generates the
   Python universal profile, installs the pinned runtime and verifies the result. */
int ampr_backend_pack_game(const char *mounted_root,const char *new_output,int level,const char *runtime,const char *learned_profiles,const ampr_backend_callbacks *callbacks,char *error,size_t error_size);
int ampr_backend_pack_game_options(const char *mounted_root,const char *new_output,int level,const char *runtime,const char *learned_profiles,int verify_output,const ampr_backend_callbacks *callbacks,char *error,size_t error_size);
int ampr_backend_thread_settings(const char *input,char *output,size_t size);
int ampr_backend_benchmark_game(const char *root,const char *destination,int level,int exhaustive,const char *learned,const ampr_backend_callbacks *callbacks,char *error,size_t error_size);
void ampr_backend_benchmark_state(char *output,size_t size);
void ampr_backend_benchmark_prepare(void);
void ampr_backend_benchmark_failed(const char *error);
int ampr_backend_benchmark_apply(const char *id,char *output,size_t size);
void ampr_backend_partial_info(char *output,size_t size);
void ampr_backend_metrics(char *output,size_t size);
int ampr_backend_recover_partial(const char *output,char *error,size_t size);
int ampr_backend_delete_partial(char *error,size_t error_size);
int ampr_backend_measure(const char *mounted_root,uint64_t *logical,uint64_t *required,char *error,size_t error_size);
int ampr_backend_copy_verified(const char *packed_root,const char *new_internal,const ampr_backend_callbacks *callbacks,char *error,size_t error_size);
int ampr_backend_copy_options(const char *packed_root,const char *new_internal,int verify_copy,const ampr_backend_callbacks *callbacks,char *error,size_t error_size);
int ampr_backend_mounted_titles(const char *json, char *titles, size_t size, char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
