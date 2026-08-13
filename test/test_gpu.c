__global__ void vec_add(float* a, float* b, float* c, int n) {
    int i = 42;
    c[i] = a[i] + b[i];
}

__device__ int helper(int x) {
    return x * 2;
}

__host__ void host_func(void) {
}

__host__ __device__ int both(void) {
    return 0;
}

__shared__ int shared_buf[256];
__constant__ float const_data[64];

int main(void) {
    int n = 1024;
    vec_add<<<256, 128>>>(0, 0, 0, n);
    return 0;
}
