/* Day 1 OpenACC smoke test: an all-pairs force sum on the GPU, checked
 * against the same loop on the host. Exit 0 = device result matches.
 * Not part of the N-body build; used only by slurm/openacc_probe.sbatch. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <openacc.h>

int main(void)
{
    const int n = 4096;
    const double eps2 = 1.0e-4;
    double *x = malloc(n * sizeof *x), *m = malloc(n * sizeof *m);
    double *a_dev = malloc(n * sizeof *a_dev), *a_host = malloc(n * sizeof *a_host);
    if (!x || !m || !a_dev || !a_host) return 1;
    for (int i = 0; i < n; ++i) { x[i] = (double)i / n; m[i] = 1.0 / n; }

    printf("acc devices (nvidia): %d\n", acc_get_num_devices(acc_device_nvidia));

    #pragma acc parallel loop copyin(x[0:n], m[0:n]) copyout(a_dev[0:n])
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        #pragma acc loop seq
        for (int j = 0; j < n; ++j) {
            const double d = x[j] - x[i];
            const double r2 = d * d + eps2;
            s += m[j] * d / (r2 * sqrt(r2));
        }
        a_dev[i] = s;
    }

    /* Error is scaled by the largest |a|: central particles have a near-zero
     * net force, where a per-element relative error is meaningless, and the
     * GPU contracts to FMA, so results differ in the last bits. */
    double max_abs = 0.0, max_diff = 0.0;
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < n; ++j) {
            const double d = x[j] - x[i];
            const double r2 = d * d + eps2;
            s += m[j] * d / (r2 * sqrt(r2));
        }
        a_host[i] = s;
        if (fabs(s) > max_abs) max_abs = fabs(s);
        if (fabs(a_dev[i] - s) > max_diff) max_diff = fabs(a_dev[i] - s);
    }
    const double max_err = max_diff / (max_abs + 1e-300);
    printf("max |device - host| / max |host|: %.3e\n", max_err);
    puts(max_err < 1e-12 ? "PASS" : "FAIL");
    free(x); free(m); free(a_dev); free(a_host);
    return max_err < 1e-12 ? 0 : 1;
}
