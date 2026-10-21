/*
 * write_mobius_h5.c
 *
 * Minimal, self-contained example of writing pedestrian trajectories in the
 * Project Mobius HDF5 layout with the HDF5 C library that ships (as source) in
 * this repository. Copy the two structs and the write_* functions into your own
 * simulator and feed them your agents instead of the demo corridor below.
 *
 * Layout reference: docs/PEDESTRIAN-DATA-FORMAT.md
 * Build guide:      docs/HDF5-EXPORT-GUIDE.md (or CMakeLists.txt next to this file)
 *
 * Usage:
 *   write_mobius_h5 [output.h5] [--positions-only]
 *
 *   --positions-only  leave out the rotation/speed/mode members so Mobius derives
 *                     heading and speed from the movement itself. Releases from
 *                     before October 2026 do that slowly for thousands of agents;
 *                     to support them, compute heading (atan2 towards the next
 *                     position) and speed yourself.
 *
 * MIT License - Copyright (c) 2026 ProjectMobius contributors
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdf5.h"

/* Fixed-length string sizes, INCLUDING the terminating NUL. Mobius reads these
 * strings as C strings, so every value must be shorter than its field. */
#define NAME_LEN  32
#define PLANE_LEN 16
#define MODE_LEN  8

/* One row of /entities. Mobius reads these six members BY POSITION with these
 * exact widths, so do not reorder them or change their types. */
typedef struct
{
    int32_t id;                /* must equal the row index: 0, 1, 2, ... */
    char    name[NAME_LEN];
    float   sim_time_s;
    float   max_speed;
    char    m_plane[PLANE_LEN];
    int32_t map;
} entity_t;

/* One row of /simulation/samples: one agent at one timestep. The first five
 * members are read BY POSITION; rotation, speed and mode are optional and are
 * looked up by name. */
typedef struct
{
    int32_t timestep_idx;      /* time = timestep_idx * sampling_rate */
    int32_t entity_id;         /* 0 .. num_entities-1 */
    float   position_x;        /* metres */
    float   position_y;        /* metres */
    float   position_z;        /* metres, height of the floor the agent stands on */
    float   rotation;          /* degrees, counter-clockwise from +x */
    float   speed;             /* metres per second, >= 0 */
    char    mode[MODE_LEN];    /* free text, e.g. "walk" (informational only) */
} sample_t;

#define CHECK(expr)                                                              \
    do                                                                           \
    {                                                                            \
        if ((expr) < 0)                                                          \
        {                                                                        \
            fprintf(stderr, "HDF5 call failed at %s:%d: %s\n", __FILE__,         \
                    __LINE__, #expr);                                            \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

static hid_t make_fixed_string(size_t size)
{
    hid_t type = H5Tcopy(H5T_C_S1);
    CHECK(type);
    CHECK(H5Tset_size(type, size));
    CHECK(H5Tset_strpad(type, H5T_STR_NULLTERM));
    return type;
}

static void write_scalar_attr(hid_t loc, const char *name, hid_t type, const void *value)
{
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t attr  = H5Acreate2(loc, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    CHECK(attr);
    CHECK(H5Awrite(attr, type, value));
    H5Aclose(attr);
    H5Sclose(space);
}

/* Write a 1-D dataset. mem_type describes the C struct; the file type is a
 * packed copy of it, so members you leave out of mem_type take no space. */
static void write_table(hid_t file, const char *path, hid_t mem_type,
                        const void *rows, hsize_t count)
{
    hid_t file_type = H5Tcopy(mem_type);
    CHECK(file_type);
    if (H5Tget_class(file_type) == H5T_COMPOUND)
    {
        CHECK(H5Tpack(file_type));
    }

    hid_t space = H5Screate_simple(1, &count, NULL);
    hid_t dcpl  = H5Pcreate(H5P_DATASET_CREATE);

    /* gzip is optional. Use it only when this HDF5 build has it; Mobius reads
     * gzip (deflate) and shuffle, and NO other compression filter. */
    if (count >= 4096 && H5Zfilter_avail(H5Z_FILTER_DEFLATE) > 0)
    {
        hsize_t chunk = 4096;
        CHECK(H5Pset_chunk(dcpl, 1, &chunk));
        CHECK(H5Pset_shuffle(dcpl));
        CHECK(H5Pset_deflate(dcpl, 4));
    }

    hid_t dset = H5Dcreate2(file, path, file_type, space, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    CHECK(dset);
    CHECK(H5Dwrite(dset, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, rows));

    H5Dclose(dset);
    H5Pclose(dcpl);
    H5Sclose(space);
    H5Tclose(file_type);
}

static void write_metadata(hid_t file, double duration_s, double sampling_interval_s,
                           int64_t num_entities)
{
    const uint8_t yes = 1;
    hid_t meta = H5Gcreate2(file, "/metadata", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    CHECK(meta);

    write_scalar_attr(meta, "duration", H5T_NATIVE_DOUBLE, &duration_s);
    /* NOTE: despite its name this is the time BETWEEN samples, in seconds (0.1 = 10 Hz). */
    write_scalar_attr(meta, "sampling_rate", H5T_NATIVE_DOUBLE, &sampling_interval_s);
    write_scalar_attr(meta, "max_num_entities", H5T_NATIVE_INT64, &num_entities);
    write_scalar_attr(meta, "is_si", H5T_NATIVE_UINT8, &yes);   /* positions in metres */
    write_scalar_attr(meta, "is_deg", H5T_NATIVE_UINT8, &yes);  /* rotation in degrees */

    H5Gclose(meta);
}

static void write_entities(hid_t file, const entity_t *entities, hsize_t count)
{
    hid_t name_type  = make_fixed_string(NAME_LEN);
    hid_t plane_type = make_fixed_string(PLANE_LEN);

    hid_t type = H5Tcreate(H5T_COMPOUND, sizeof(entity_t));
    CHECK(type);
    CHECK(H5Tinsert(type, "id", offsetof(entity_t, id), H5T_NATIVE_INT32));
    CHECK(H5Tinsert(type, "name", offsetof(entity_t, name), name_type));
    CHECK(H5Tinsert(type, "sim_time_s", offsetof(entity_t, sim_time_s), H5T_NATIVE_FLOAT));
    CHECK(H5Tinsert(type, "max_speed", offsetof(entity_t, max_speed), H5T_NATIVE_FLOAT));
    CHECK(H5Tinsert(type, "m_plane", offsetof(entity_t, m_plane), plane_type));
    CHECK(H5Tinsert(type, "map", offsetof(entity_t, map), H5T_NATIVE_INT32));

    write_table(file, "/entities", type, entities, count);

    H5Tclose(type);
    H5Tclose(plane_type);
    H5Tclose(name_type);
}

static void write_samples(hid_t file, const sample_t *samples, hsize_t count,
                          int with_rotation_speed_mode)
{
    hid_t mode_type = make_fixed_string(MODE_LEN);

    hid_t type = H5Tcreate(H5T_COMPOUND, sizeof(sample_t));
    CHECK(type);
    CHECK(H5Tinsert(type, "timestep_idx", offsetof(sample_t, timestep_idx), H5T_NATIVE_INT32));
    CHECK(H5Tinsert(type, "entity_id", offsetof(sample_t, entity_id), H5T_NATIVE_INT32));
    CHECK(H5Tinsert(type, "position_x", offsetof(sample_t, position_x), H5T_NATIVE_FLOAT));
    CHECK(H5Tinsert(type, "position_y", offsetof(sample_t, position_y), H5T_NATIVE_FLOAT));
    CHECK(H5Tinsert(type, "position_z", offsetof(sample_t, position_z), H5T_NATIVE_FLOAT));
    if (with_rotation_speed_mode)
    {
        CHECK(H5Tinsert(type, "rotation", offsetof(sample_t, rotation), H5T_NATIVE_FLOAT));
        CHECK(H5Tinsert(type, "speed", offsetof(sample_t, speed), H5T_NATIVE_FLOAT));
        CHECK(H5Tinsert(type, "mode", offsetof(sample_t, mode), mode_type));
    }

    write_table(file, "/simulation/samples", type, samples, count);

    H5Tclose(type);
    H5Tclose(mode_type);
}

/* ------------------------------------------------------------------------- */
/* Demo scenario: three people in a 10 m corridor, sampled every 0.1 s.      */
/* Replace this part with your own simulator's output.                       */
/* ------------------------------------------------------------------------- */

#define NUM_ENTITIES 3

typedef struct
{
    float start_time;  /* s */
    float start_x;     /* m */
    float lane_y;      /* m */
    float velocity_x;  /* m/s, sign gives the direction */
} demo_agent_t;

static const demo_agent_t demo_agents[NUM_ENTITIES] = {
    { 0.0f,  0.0f, 1.0f,  1.2f },  /* leaves the corridor at x = 10 m, then has no rows */
    { 0.0f,  0.0f, 2.0f,  1.0f },
    { 2.0f, 10.0f, 3.0f, -1.3f },  /* enters at t = 2 s walking towards -x */
};

int main(int argc, char **argv)
{
    const char *out_path = "mobius_example.h5";
    int with_rotation_speed_mode = 1;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--positions-only") == 0)
        {
            with_rotation_speed_mode = 0;
        }
        else
        {
            out_path = argv[i];
        }
    }

    const double dt = 0.1;           /* sampling interval, s */
    const int32_t num_timesteps = 101; /* t = 0.0 .. 10.0 s */

    entity_t entities[NUM_ENTITIES];
    int32_t first_step[NUM_ENTITIES];
    int32_t last_step[NUM_ENTITIES];
    memset(entities, 0, sizeof(entities));

    for (int32_t e = 0; e < NUM_ENTITIES; ++e)
    {
        entities[e].id = e;
        snprintf(entities[e].name, NAME_LEN, "Agent %d", e);
        snprintf(entities[e].m_plane, PLANE_LEN, "Floor 0");
        entities[e].max_speed = fabsf(demo_agents[e].velocity_x);
        entities[e].map = 0;
        first_step[e] = -1;
        last_step[e] = -1;
    }

    size_t capacity = (size_t)num_timesteps * NUM_ENTITIES;
    sample_t *samples = (sample_t *)calloc(capacity, sizeof(sample_t));
    int32_t *samples_per_timestep = (int32_t *)calloc((size_t)num_timesteps, sizeof(int32_t));
    float *timesteps = (float *)calloc((size_t)num_timesteps, sizeof(float));
    if (!samples || !samples_per_timestep || !timesteps)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    /* Rows are written timestep by timestep. An agent that is not in the
     * building at a timestep simply has no row there. */
    size_t count = 0;
    for (int32_t step = 0; step < num_timesteps; ++step)
    {
        const float t = (float)(step * dt);
        timesteps[step] = t;

        for (int32_t e = 0; e < NUM_ENTITIES; ++e)
        {
            const demo_agent_t *a = &demo_agents[e];
            if (t < a->start_time)
            {
                continue;
            }
            const float x = a->start_x + a->velocity_x * (t - a->start_time);
            if (x < 0.0f || x > 10.0f)
            {
                continue;
            }

            sample_t *s = &samples[count++];
            s->timestep_idx = step;
            s->entity_id = e;
            s->position_x = x;
            s->position_y = a->lane_y;
            s->position_z = 0.0f;
            /* Heading: atan2(dy, dx) in degrees, so +x is 0 and -x is 180. */
            s->rotation = a->velocity_x >= 0.0f ? 0.0f : 180.0f;
            s->speed = fabsf(a->velocity_x);
            snprintf(s->mode, MODE_LEN, "walk");

            ++samples_per_timestep[step];
            if (first_step[e] < 0)
            {
                first_step[e] = step;
            }
            last_step[e] = step;
        }
    }

    for (int32_t e = 0; e < NUM_ENTITIES; ++e)
    {
        entities[e].sim_time_s = first_step[e] < 0 ? 0.0f : (float)((last_step[e] - first_step[e]) * dt);
    }

    hid_t file = H5Fcreate(out_path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    CHECK(file);

    /* duration = time of the LAST timestep = (num_timesteps - 1) * dt */
    write_metadata(file, (num_timesteps - 1) * dt, dt, NUM_ENTITIES);
    write_entities(file, entities, NUM_ENTITIES);

    hid_t sim = H5Gcreate2(file, "/simulation", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    CHECK(sim);
    H5Gclose(sim);

    write_samples(file, samples, count, with_rotation_speed_mode);
    write_table(file, "/simulation/timesteps", H5T_NATIVE_FLOAT, timesteps, (hsize_t)num_timesteps);
    write_table(file, "/simulation/samples_per_timestep", H5T_NATIVE_INT32,
                samples_per_timestep, (hsize_t)num_timesteps);

    CHECK(H5Fclose(file));

    printf("Wrote %s: %d entities, %d timesteps, %zu samples%s\n", out_path, NUM_ENTITIES,
           num_timesteps, count, with_rotation_speed_mode ? "" : " (positions only)");

    free(timesteps);
    free(samples_per_timestep);
    free(samples);
    return 0;
}
