/* The engine script functions the translated retail modules call (retail: the
 * `stingray` table handed to each module), as one table of pointers the mod
 * fills in. One pointer per engine function, named after it; handles are
 * opaque. Declared so far: what head_visibility.c uses (model_shadow_source.c
 * calls no engine function). Add other modules' functions below; nothing
 * here depends on any other header of the project.
 *
 * Every function returns false when the Lua call raised: `error->text` is
 * then the error's text. A pointer left NULL is a function the engine does
 * not have (`sr.Unit.has_mesh == nil`). */
#ifndef TFP_ENGINE_IFACE_H
#define TFP_ENGINE_IFACE_H
#include <stdbool.h>
#include <stddef.h>

#define TFP_ENGINE_ERROR_TEXT 256
typedef struct TfpEngineError { char text[TFP_ENGINE_ERROR_TEXT]; } TfpEngineError;

/* Engine userdata. NULL is Lua nil. */
typedef const struct TfpEngineWorldObject *TfpEngineWorld;
typedef const struct TfpEngineUnitObject *TfpEngineUnit;
typedef const struct TfpEngineMeshObject *TfpEngineMesh;

typedef struct TfpEngineIface {
  void *user;
  /* stingray.Application.main_world() -> World or nil */
  bool (*Application_main_world)(void *user, TfpEngineWorld *world, TfpEngineError *error);
  /* stingray.World.units(world) -> array of Unit. *units stays valid until the next call of this
   * function; *units NULL with true: the engine answered something that is not a table. */
  bool (*World_units)(void *user, TfpEngineWorld world, const TfpEngineUnit **units, size_t *count, TfpEngineError *error);
  /* stingray.Unit.set_mesh_visibility(unit, mesh_index, visible, pass_name): mesh_index counts from 1;
   * pass_name is "default" (camera) or "shadow_caster". */
  bool (*Unit_set_mesh_visibility)(void *user, TfpEngineUnit unit, int mesh_index, bool visible, const char *pass_name,
                                   TfpEngineError *error);
  /* stingray.Unit.has_mesh(unit, mesh_name) -> boolean */
  bool (*Unit_has_mesh)(void *user, TfpEngineUnit unit, const char *mesh_name, bool *present, TfpEngineError *error);
  /* stingray.Unit.mesh(unit, mesh_name) -> Mesh or nil */
  bool (*Unit_mesh)(void *user, TfpEngineUnit unit, const char *mesh_name, TfpEngineMesh *mesh, TfpEngineError *error);
  /* stingray.Mesh.name(mesh) -> number (the 32-bit name hash; the caller reduces it modulo 2^32) */
  bool (*Mesh_name)(void *user, TfpEngineMesh mesh, double *name, TfpEngineError *error);
  /* stingray.Mesh.node(mesh) -> number (scene-graph node index) */
  bool (*Mesh_node)(void *user, TfpEngineMesh mesh, double *node, TfpEngineError *error);
  /* stingray.Mesh.num_materials(mesh) -> number */
  bool (*Mesh_num_materials)(void *user, TfpEngineMesh mesh, double *count, TfpEngineError *error);
  /* stingray.Mesh.has_material(mesh, material_name) -> boolean */
  bool (*Mesh_has_material)(void *user, TfpEngineMesh mesh, const char *material_name, bool *present, TfpEngineError *error);
} TfpEngineIface;
#endif
