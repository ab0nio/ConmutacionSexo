#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

#define MAX_LINE 100

typedef struct TrieNode {
    int interface; // -1 si no es nodo válido
    struct TrieNode *left;
    struct TrieNode *right;
} TrieNode;

TrieNode* create_trienode() {
    TrieNode *node = (TrieNode*) malloc(sizeof(TrieNode));
    node->interface = -1;
    node->left = NULL;
    node->right = NULL;
    return node;
}

int get_bit(uint32_t ip, int bit_index) {
    return (ip >> (31 - bit_index)) & 1;
}

TrieNode* create_trie(const char* rfile) {
    FILE *f = fopen(rfile, "r");
    if (!f) {
        perror("Error abriendo archivo de rutas");
        exit(1);
    }

    TrieNode *root = create_trienode();
    char line[MAX_LINE];

    while (fgets(line, MAX_LINE, f)) {
        char *prefix_sep = strchr(line, '/');
        if (!prefix_sep) continue;

        int prefix_len;
        sscanf(prefix_sep + 1, "%d", &prefix_len);
        *prefix_sep = '\0';

        char *iface_str = strchr(prefix_sep + 1, '\t');
        if (!iface_str) iface_str = strchr(prefix_sep + 1, ' ');
        if (!iface_str) continue;

        int iface;
        sscanf(iface_str, "%d", &iface);

        unsigned int a, b, c, d;
        if (sscanf(line, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) continue;
        uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;

        TrieNode *current = root;
        for (int i = 0; i < prefix_len; i++) {
            int bit = get_bit(ip, i);
            if (bit == 0) {
                if (!current->left) current->left = create_trienode();
                current = current->left;
            } else {
                if (!current->right) current->right = create_trienode();
                current = current->right;
            }
        }
        current->interface = iface;
    }

    fclose(f);
    return root;
}

TrieNode* compress_trie_helper(TrieNode* node, int depth, int* total_nodes) {
    if (!node) return NULL;

    (*total_nodes)++;

    node->left = compress_trie_helper(node->left, depth + 1, total_nodes);
    node->right = compress_trie_helper(node->right, depth + 1, total_nodes);

    if (node->interface == -1) {
        if (node->left && !node->right) {
            TrieNode *child = node->left;
            free(node);
            return child;
        } else if (!node->left && node->right) {
            TrieNode *child = node->right;
            free(node);
            return child;
        }
    }

    return node;
}

void compress_trie(TrieNode **root) {
    int total_nodes = 0;
    *root = compress_trie_helper(*root, 0, &total_nodes);
}

int count_trie(TrieNode *t) {
    if (!t) return 0;
    return 1 + count_trie(t->left) + count_trie(t->right);
}

int lookup_verbose(TrieNode *t, uint32_t ip, int *prefix_len, int *nodes_visited) {
    TrieNode *current = t;
    int best_match = -1;
    int best_length = 0;

    *nodes_visited = 0;

    for (int i = 0; i < 32 && current; i++) {
        if (current->interface != -1) {
            best_match = current->interface;
            best_length = i;
        }

        int bit = get_bit(ip, i);
        current = (bit == 0) ? current->left : current->right;
        (*nodes_visited)++;
    }

    if (current && current->interface != -1) {
        best_match = current->interface;
        best_length = 32;
        (*nodes_visited)++;
    }

    *prefix_len = best_length;
    return best_match;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <routing_table.txt> <packets.txt>\n", argv[0]);
        return 1;
    }

    struct rusage start_cpu, end_cpu;
    getrusage(RUSAGE_SELF, &start_cpu);

    TrieNode *root = create_trie(argv[1]);
    compress_trie(&root);

    int total_nodes = count_trie(root);

    FILE *fin = fopen(argv[2], "r");
    if (!fin) {
        perror("Error abriendo archivo de paquetes");
        return 1;
    }

    char out_filename[200];
    snprintf(out_filename, sizeof(out_filename), "%s.out", argv[2]);
    FILE *fout = fopen(out_filename, "w");
    if (!fout) {
        perror("Error creando archivo de salida");
        return 1;
    }

    char line[MAX_LINE];
    int total_packets = 0;
    int total_visited = 0;
    long total_time_ns = 0;

    while (fgets(line, MAX_LINE, fin)) {
        unsigned int a, b, c, d;
        if (sscanf(line, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;
            int prefix_len, nodes_visited;
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);

            int iface = lookup_verbose(root, ip, &prefix_len, &nodes_visited);

            clock_gettime(CLOCK_MONOTONIC, &end);
            long elapsed_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);

            total_packets++;
            total_visited += nodes_visited > 0 ? nodes_visited - 1 : 0;  // sin contar la raíz
            total_time_ns += elapsed_ns;

            if (iface == -1) {
                fprintf(fout, "%u.%u.%u.%u;MISS;%d;%ld\n", a, b, c, d, nodes_visited - 1, elapsed_ns);
            } else {
                fprintf(fout, "%u.%u.%u.%u;%d;%d;%ld\n", a, b, c, d, iface, nodes_visited - 1, elapsed_ns);
            }
        }
    }

    fprintf(fout, "\n");
    double avg_access = total_packets ? (double)total_visited / total_packets : 0;
    double avg_time = total_packets ? (double)total_time_ns / total_packets : 0;
    size_t mem_kb = total_nodes * sizeof(TrieNode) / 1024;

    getrusage(RUSAGE_SELF, &end_cpu);
    double cpu_secs = (end_cpu.ru_utime.tv_sec - start_cpu.ru_utime.tv_sec)
                    + (end_cpu.ru_utime.tv_usec - start_cpu.ru_utime.tv_usec) / 1e6;

    fprintf(fout, "Number of nodes in the tree = %d\n", total_nodes);
    fprintf(fout, "Packets processed= %d\n", total_packets);
    fprintf(fout, "Average node accesses= %.2f\n", avg_access);
    fprintf(fout, "Average packet processing time (nsecs)= %.2f\n", avg_time);
    fprintf(fout, "Memory (Kbytes) = %zu\n", mem_kb);
    fprintf(fout, "CPU Time (secs)= %.6f\n", cpu_secs);

    fclose(fin);
    fclose(fout);
    return 0;
}
