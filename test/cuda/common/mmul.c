#include <cuda.h>
#ifdef __KERNEL__ /* just for measurement */
#include <linux/vmalloc.h>
#include <linux/time.h>
#define printf printk
#define malloc vmalloc
#define free vfree
#define gettimeofday(x, y) do_gettimeofday(x)
#else /* just for measurement */
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#endif

/* tvsub: ret = x - y. */
static inline void tvsub(struct timeval *x, 
						 struct timeval *y, 
						 struct timeval *ret)
{
	ret->tv_sec = x->tv_sec - y->tv_sec;
	ret->tv_usec = x->tv_usec - y->tv_usec;
	if (ret->tv_usec < 0) {
		ret->tv_sec--;
		ret->tv_usec += 1000000;
	}
}

int cuda_test_mmul(unsigned int n, char *path)
{
	int i, j, idx;
	CUresult res;
	CUdevice dev;
	CUcontext ctx;
	CUfunction function;
	CUmodule module;
	CUdeviceptr a_dev, b_dev, c_dev;
	unsigned int *a = (unsigned int *) malloc (n*n * sizeof(unsigned int));
	unsigned int *b = (unsigned int *) malloc (n*n * sizeof(unsigned int));
	unsigned int *c = (unsigned int *) malloc (n*n * sizeof(unsigned int));
	int block_x, block_y, grid_x, grid_y;
	char fname[256];
	struct timeval tv;
	struct timeval tv_total_start, tv_total_end;
	float total;
	struct timeval tv_h2d_start, tv_h2d_end;
	float h2d;
	struct timeval tv_d2h_start, tv_d2h_end;
	float d2h;
	struct timeval tv_exec_start, tv_exec_end;
	float exec;

	/* block_x * block_y should not exceed 512 (16 warps). 
	   fermi allows 1024 threads (32 warps). */
	block_x = n < 16 ? n : 16;
	block_y = n < 16 ? n : 16;
	grid_x = n / block_x;
	if (n % block_x != 0)
		grid_x++;
	grid_y = n / block_y;
	if (n % block_y != 0)
		grid_y++;
	printf("block = (%d, %d)\n", block_x, block_y);
	printf("grid = (%d, %d)\n", grid_x, grid_y);

	gettimeofday(&tv_total_start, NULL);

	res = cuInit(0);
	if (res != CUDA_SUCCESS) {
		printf("cuInit failed: res = %lu\n", (unsigned long)res);
		return -1;
	}

	res = cuDeviceGet(&dev, 0);
	if (res != CUDA_SUCCESS) {
		printf("cuDeviceGet failed: res = %lu\n", (unsigned long)res);
		return -1;
	}

	res = cuCtxCreate(&ctx, 0, dev);
	if (res != CUDA_SUCCESS) {
		printf("cuCtxCreate failed: res = %lu\n", (unsigned long)res);
		return -1;
	}

	sprintf(fname, "%s/mmul_gpu.cubin", path);
	res = cuModuleLoad(&module, fname);
	if (res != CUDA_SUCCESS) {
		printf("cuModuleLoad() failed\n");
		return -1;
	}
	res = cuModuleGetFunction(&function, module, "_Z8multiplyPjS_S_i");
	if (res != CUDA_SUCCESS) {
		printf("cuModuleGetFunction() failed\n");
		return -1;
	}
	res = cuFuncSetSharedSize(function, 0x40); /* just random */
	if (res != CUDA_SUCCESS) {
		printf("cuFuncSetSharedSize() failed\n");
		return -1;
	}
	res = cuFuncSetBlockShape(function, block_x, block_y, 1);
	if (res != CUDA_SUCCESS) {
		printf("cuFuncSetBlockShape() failed\n");
		return -1;
	}

	/* a[] */
	res = cuMemAlloc(&a_dev, n*n * sizeof(unsigned int));
	if (res != CUDA_SUCCESS) {
		printf("cuMemAlloc (a) failed\n");
		return -1;
	}
	/* b[] */
	res = cuMemAlloc(&b_dev, n*n * sizeof(unsigned int));
	if (res != CUDA_SUCCESS) {
		printf("cuMemAlloc (b) failed\n");
		return -1;
	}
	/* c[] */
	res = cuMemAlloc(&c_dev, n*n * sizeof(unsigned int));
	if (res != CUDA_SUCCESS) {
		printf("cuMemAlloc (c) failed\n");
		return -1;
	}

	gettimeofday(&tv_h2d_start, NULL);
	/* upload a[] and b[] */
	res = cuMemcpyHtoD(a_dev, a, n*n * sizeof(unsigned int));
	if (res != CUDA_SUCCESS) {
		printf("cuMemcpyHtoD (a) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuMemcpyHtoD(b_dev, b, n*n * sizeof(unsigned int));
	if (res != CUDA_SUCCESS) {
		printf("cuMemcpyHtoD (b) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	gettimeofday(&tv_h2d_end, NULL);

	/* set kernel parameters */
	res = cuParamSeti(function, 0, a_dev);	
	if (res != CUDA_SUCCESS) {
		printf("cuParamSeti (a) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuParamSeti(function, 4, a_dev >> 32);
	if (res != CUDA_SUCCESS) {
		printf("cuParamSeti (a) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuParamSeti(function, 8, b_dev);
	if (res != CUDA_SUCCESS) {
		printf("cuParamSeti (b) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuParamSeti(function, 12, b_dev >> 32);
	if (res != CUDA_SUCCESS) {
		printf("cuParamSeti (b) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuParamSeti(function, 16, c_dev);
	if (res != CUDA_SUCCESS) {
		printf("cuParamSeti (c) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuParamSeti(function, 20, c_dev >> 32);
	if (res != CUDA_SUCCESS) {
		printf("cuParamSeti (c) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuParamSeti(function, 24, n);
	if (res != CUDA_SUCCESS) {
		printf("cuParamSeti (c) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuParamSetSize(function, 28);
	if (res != CUDA_SUCCESS) {
		printf("cuParamSetSize failed: res = %lu\n", (unsigned long)res);
		return -1;
	}

	gettimeofday(&tv_exec_start, NULL);
	/* launch the kernel */
	res = cuLaunchGrid(function, grid_x, grid_y);
	if (res != CUDA_SUCCESS) {
		printf("cuLaunchGrid failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	cuCtxSynchronize();
	gettimeofday(&tv_exec_end, NULL);

	gettimeofday(&tv_d2h_start, NULL);
	/* download c[] */
	res = cuMemcpyDtoH(c, c_dev, n*n * sizeof(unsigned int));
	if (res != CUDA_SUCCESS) {
		printf("cuMemcpyDtoH (c) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	gettimeofday(&tv_d2h_end, NULL);

	res = cuMemFree(a_dev);
	if (res != CUDA_SUCCESS) {
		printf("cuMemFree (a) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuMemFree(b_dev);
	if (res != CUDA_SUCCESS) {
		printf("cuMemFree (b) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}
	res = cuMemFree(c_dev);
	if (res != CUDA_SUCCESS) {
		printf("cuMemFree (c) failed: res = %lu\n", (unsigned long)res);
		return -1;
	}

	res = cuModuleUnload(module);
	if (res != CUDA_SUCCESS) {
		printf("cuModuleUnload failed: res = %lu\n", (unsigned long)res);
		return -1;
	}

	res = cuCtxDestroy(ctx);
	if (res != CUDA_SUCCESS) {
		printf("cuCtxDestroy failed: res = %lu\n", (unsigned long)res);
		return -1;
	}

	gettimeofday(&tv_total_end, NULL);

	/* check the results */
	i = j = idx = 0;
	while (i < n) {
		while (j < n) {
			idx = i * n + j;
			if (c[idx] != a[idx] * b[idx]) {
				return -1;
			}
			j++;
		}
		i++;
	}

	free(a);
	free(b);
	free(c);

	tvsub(&tv_h2d_end, &tv_h2d_start, &tv);
	h2d = tv.tv_sec * 1000.0 + (float) tv.tv_usec / 1000.0;
	tvsub(&tv_d2h_end, &tv_d2h_start, &tv);
	d2h = tv.tv_sec * 1000.0 + (float) tv.tv_usec / 1000.0;
	tvsub(&tv_exec_end, &tv_exec_start, &tv);
	exec = tv.tv_sec * 1000.0 + (float) tv.tv_usec / 1000.0;
	tvsub(&tv_total_end, &tv_total_start, &tv);
	total = tv.tv_sec * 1000.0 + (float) tv.tv_usec / 1000.0;

	printf("HtoD: %f\n", h2d);
	printf("DtoH: %f\n", d2h);
	printf("Exec: %f\n", exec);
	printf("Time (Memcpy + Launch): %f\n", h2d + d2h + exec);
	printf("Total: %f\n", total);

	return 0;
}
