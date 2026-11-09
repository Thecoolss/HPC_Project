#include "nbody.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_initialization(void)
{
    Particles first = {0};
    Particles second = {0};
    Particles invalid = {0};
    assert(!particles_alloc(&invalid, 0));
    assert(invalid.n == 0 && invalid.x == NULL && invalid.mass == NULL);
    assert(particles_alloc(&first, 64));
    assert(particles_alloc(&second, 64));
    assert(particles_init_cube(&first, UINT64_C(12345)));
    assert(particles_init_cube(&second, UINT64_C(12345)));

    first.vx[0] = 1.0;
    first.ay[0] = 1.0;
    assert(particles_init_cube(&first, UINT64_C(12345)));
    double mass_sum = 0.0;
    for (int i = 0; i < first.n; ++i) {
        assert(first.x[i] == second.x[i]);
        assert(first.y[i] == second.y[i]);
        assert(first.z[i] == second.z[i]);
        assert(first.mass[i] == second.mass[i]);
        assert(first.x[i] >= -1.0 && first.x[i] < 1.0);
        assert(first.y[i] >= -1.0 && first.y[i] < 1.0);
        assert(first.z[i] >= -1.0 && first.z[i] < 1.0);
        assert(first.mass[i] > 0.0);
        assert(first.vx[i] == 0.0 && first.vy[i] == 0.0 &&
               first.vz[i] == 0.0);
        assert(first.ax[i] == 0.0 && first.ay[i] == 0.0 &&
               first.az[i] == 0.0);
        mass_sum += first.mass[i];
    }
    assert(fabs(mass_sum - 1.0) < 1.0e-14);

    particles_free(&first);
    particles_free(&second);
}

static void test_single_particle(void)
{
    Particles p = {0};
    assert(particles_alloc(&p, 1));
    assert(particles_init_cube(&p, UINT64_C(7)));
    const double x = p.x[0];
    const double y = p.y[0];
    const double z = p.z[0];

    compute_accelerations_serial(&p, 0, 1, 1.0, 0.01);
    assert(p.ax[0] == 0.0 && p.ay[0] == 0.0 && p.az[0] == 0.0);
    particles_kick(&p, 0, 1, 0.0005);
    particles_drift(&p, 0, 1, 0.001);
    assert(p.x[0] == x && p.y[0] == y && p.z[0] == z);

    particles_free(&p);
}

static void set_two_body_fixture(Particles *p)
{
    assert(particles_alloc(p, 2));
    p->x[0] = -0.5;
    p->x[1] = 0.5;
    p->mass[0] = 0.5;
    p->mass[1] = 0.5;
    p->vy[0] = 0.2;
    p->vy[1] = -0.2;
}

static void test_symmetric_force_and_range(void)
{
    Particles p = {0};
    set_two_body_fixture(&p);
    compute_accelerations_serial(&p, 0, 2, 1.0, 0.1);
    const double expected = 0.5 / (1.01 * sqrt(1.01));
    assert(fabs(p.ax[0] - expected) < 1.0e-14);
    assert(fabs(p.ax[0] + p.ax[1]) < 1.0e-14);
    assert(p.ay[0] == -p.ay[1]);
    assert(p.az[0] == -p.az[1]);

    particles_free(&p);
    assert(particles_alloc(&p, 3));
    p.x[0] = 0.0;
    p.x[1] = 1.0;
    p.x[2] = 2.0;
    p.mass[0] = p.mass[1] = p.mass[2] = 1.0;
    p.ax[0] = p.ay[0] = p.az[0] = -77.0;
    p.ax[1] = p.ay[1] = p.az[1] = -77.0;
    p.ax[2] = p.ay[2] = p.az[2] = -77.0;
    compute_accelerations_serial(&p, 1, 1, 1.0, 0.1);
    assert(p.ax[0] == -77.0 && p.ax[1] == -77.0 && p.ax[2] == -77.0);
    compute_accelerations_serial(&p, 1, 2, 1.0, 0.1);
    assert(p.ax[0] == -77.0 && p.ay[0] == -77.0 && p.az[0] == -77.0);
    assert(p.ax[2] == -77.0 && p.ay[2] == -77.0 && p.az[2] == -77.0);
    assert(p.ax[1] != -77.0);
    particles_free(&p);
}

static void test_momentum_and_energy_drift(void)
{
    Particles p = {0};
    set_two_body_fixture(&p);
    const double initial_energy = compute_total_energy(&p, 1.0, 0.1);
    compute_accelerations_serial(&p, 0, p.n, 1.0, 0.1);

    for (int step = 0; step < 1000; ++step) {
        particles_kick(&p, 0, p.n, 0.0005);
        particles_drift(&p, 0, p.n, 0.001);
        compute_accelerations_serial(&p, 0, p.n, 1.0, 0.1);
        particles_kick(&p, 0, p.n, 0.0005);
    }

    double momentum[3];
    compute_total_momentum(&p, momentum);
    assert(fabs(momentum[0]) < 1.0e-12);
    assert(fabs(momentum[1]) < 1.0e-12);
    assert(fabs(momentum[2]) < 1.0e-12);

    const double final_energy = compute_total_energy(&p, 1.0, 0.1);
    assert(fabs(final_energy - initial_energy) / fabs(initial_energy) < 1.0e-4);
    particles_free(&p);
}

int main(void)
{
    test_initialization();
    test_single_particle();
    test_symmetric_force_and_range();
    test_momentum_and_energy_drift();
    puts("All nbody unit tests passed");
    return 0;
}
