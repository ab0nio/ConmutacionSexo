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
        uint32_t ip;
        int prefix_len, iface;
        // Parsear línea: "192.168.1.0/24 3"
        if (sscanf(line, "%u.%u.%u.%u/%d %d", 
                   (unsigned int*)&((char*)&ip)[3], 
                   (unsigned int*)&((char*)&ip)[2], 
                   (unsigned int*)&((char*)&ip)[1], 
                   (unsigned int*)&((char*)&ip)[0], 
                   &prefix_len, &iface) == 6) {

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
    }

    fclose(f);
    return root;
}
#include <stdbool.h>

TrieNode* compress_trie_helper(TrieNode* node, int depth, int* total_nodes) {
    if (!node) return NULL;

    (*total_nodes)++; // Contar nodo antes de posibles compresiones

    // Comprimir recursivamente los hijos
    node->left = compress_trie_helper(node->left, depth + 1, total_nodes);
    node->right = compress_trie_helper(node->right, depth + 1, total_nodes);

    // Nodo NO final y solo un hijo -> se puede comprimir
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
int lookup(TrieNode *t, uint32_t ip) {
    TrieNode *current = t;
    int best_match = -1;

    for (int i = 0; i < 32 && current; i++) {
        if (current->interface != -1)
            best_match = current->interface;

        int bit = get_bit(ip, i);
        current = (bit == 0) ? current->left : current->right;
    }

    // Revisar si el último nodo tiene interfaz válida
    if (current && current->interface != -1)
        best_match = current->interface;

    return best_match;
}
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <routing_table.txt> <packets.txt>\n", argv[0]);
        return 1;
    }

    TrieNode *root = create_trie(argv[1]); // Construir trie desde tabla de rutas

    FILE *f = fopen(argv[2], "r");
    if (!f) {
        perror("Error abriendo archivo de paquetes");
        return 1;
    }

    char line[MAX_LINE];
    while (fgets(line, MAX_LINE, f)) {
        uint32_t ip;
        if (sscanf(line, "%u.%u.%u.%u", 
                   (unsigned int*)&((char*)&ip)[3],
                   (unsigned int*)&((char*)&ip)[2],
                   (unsigned int*)&((char*)&ip)[1],
                   (unsigned int*)&((char*)&ip)[0]) == 4) {

            int iface = lookup(root, ip);
            printf("%u.%u.%u.%u -> %d\n",
                   ((unsigned char*)&ip)[3],
                   ((unsigned char*)&ip)[2],
                   ((unsigned char*)&ip)[1],
                   ((unsigned char*)&ip)[0],
                   iface);
        }
    }

    fclose(f);
    return 0;
}
