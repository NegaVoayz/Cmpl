int main() {
    kernel<<<1, 256>>>(a, b);

    kernel2<<<grid_dim, block_dim, 1024, stream>>>(ptr, size);

    return 0;
}
