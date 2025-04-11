#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
        char ip_str[32];
        int iface;
        char *prefix_sep = strchr(line, '/');
        if (!prefix_sep) continue;

        int prefix_len;
        sscanf(prefix_sep + 1, "%d", &prefix_len);
        *prefix_sep = '\0'; // Cortar en '/' para dejar solo la IP

        // Extraer interfaz (después del prefijo, separado por tabulador o espacio)
        char *iface_str = strchr(prefix_sep + 1, '\t');
        if (!iface_str) iface_str = strchr(prefix_sep + 1, ' ');
        if (!iface_str) continue;
        sscanf(iface_str, "%d", &iface);

        // Parsear IP a uint32_t
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

// ---------- COMPRESS ----------
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

// ---------- COUNT ----------
int count_trie(TrieNode *t) {
    if (!t) return 0;
    return 1 + count_trie(t->left) + count_trie(t->right);
}

// ---------- LOOKUP ----------
int lookup_verbose(TrieNode *t, uint32_t ip, int *prefix_len, int *nodes_visited) {
    TrieNode *current = t;
    int best_match = -1;
    int best_length = 0;

    *nodes_visited = 0;

    for (int i = 0; i < 32 && current; i++) {
        (*nodes_visited)++;
        if (current->interface != -1) {
            best_match = current->interface;
            best_length = i;
        }

        int bit = get_bit(ip, i);
        current = (bit == 0) ? current->left : current->right;
    }

    if (current && current->interface != -1) {
        best_match = current->interface;
        best_length = 32;
        (*nodes_visited)++;
    }

    *prefix_len = best_length;
    return best_match;
}

// ---------- MAIN ----------
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <routing_table.txt> <packets.txt>\n", argv[0]);
        return 1;
    }

    TrieNode *root = create_trie(argv[1]);

    int original_nodes = count_trie(root);
    compress_trie(&root);
    int compressed_nodes = count_trie(root);

    printf("Nodos antes de comprimir: %d\n", original_nodes);
    printf("Nodos después de comprimir: %d\n", compressed_nodes);

    FILE *f = fopen(argv[2], "r");
    if (!f) {
        perror("Error abriendo archivo de paquetes");
        return 1;
    }

    char line[MAX_LINE];
    while (fgets(line, MAX_LINE, f)) {
        unsigned int a, b, c, d;
        if (sscanf(line, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;

            int prefix_len, nodes_visited;
            int iface = lookup_verbose(root, ip, &prefix_len, &nodes_visited);

            printf("%u.%u.%u.%u;%d;%d;%d\n", a, b, c, d, iface, prefix_len, nodes_visited);
        }
    }

    fclose(f);
    return 0;
}

