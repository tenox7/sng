/* /proc/meminfo memory and swap, shared by Linux and the generic Unix fallback.
 * Shmem is added back because tmpfs sits inside Cached but cannot be evicted.
 * MemAvailable is avoided: it does not exist before 3.14. SReclaimable (2.6.19)
 * and Shmem (2.6.32) stay 0 when absent. */

static int meminfo_field(const char *line, const char *key, unsigned long *out) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return 0;
    return sscanf(line + klen, " %lu kB", out) == 1;
}

int os_memory_get_stats_dual(double *used_value, double *swap_value) {
    FILE *fp;
    char line[256];
    unsigned long mem_total, mem_free, buffers, cached, sreclaim, shmem;
    unsigned long swap_total, swap_free, reclaimable, used_kb, swap_used;

    if (!used_value || !swap_value) return 0;

    fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;

    mem_total = 0; mem_free = 0; buffers = 0; cached = 0;
    sreclaim = 0; shmem = 0; swap_total = 0; swap_free = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (meminfo_field(line, "MemTotal:", &mem_total)) continue;
        if (meminfo_field(line, "MemFree:", &mem_free)) continue;
        if (meminfo_field(line, "Buffers:", &buffers)) continue;
        if (meminfo_field(line, "Cached:", &cached)) continue;
        if (meminfo_field(line, "SReclaimable:", &sreclaim)) continue;
        if (meminfo_field(line, "Shmem:", &shmem)) continue;
        if (meminfo_field(line, "SwapTotal:", &swap_total)) continue;
        if (meminfo_field(line, "SwapFree:", &swap_free)) continue;
    }
    fclose(fp);

    if (mem_total == 0) return 0;

    reclaimable = mem_free + buffers + cached + sreclaim;
    if (shmem > reclaimable) shmem = reclaimable;
    reclaimable -= shmem;
    used_kb = mem_total > reclaimable ? mem_total - reclaimable : 0;

    *used_value = (double)used_kb / (double)mem_total * 100.0;
    OS_CLAMP_PCT(*used_value);

    swap_used = swap_total > swap_free ? swap_total - swap_free : 0;
    *swap_value = swap_total ? (double)swap_used / (double)swap_total * 100.0 : 0.0;
    OS_CLAMP_PCT(*swap_value);

    return 1;
}

int os_memory_get_stats(double *value) {
    double swap;
    return os_memory_get_stats_dual(value, &swap);
}
