#include "cc.h"

// Forward declaration of the helper
struct StructEnv* lookup_struct(const char* name);

// Resolves the absolute byte offset and structural type of a member access chain
// Returns 1 on success, 0 on failure
int resolve_member_offset(ASTNode* node, int* total_offset, DataType* out_type) {
	if (!node) return 0;

	// Base Case: We hit the root identifier variable
	if (node->kind == AST_IDENT) {
		Symbol* sym = lookup_symbol(node->name);
		if (!sym) {
			printf("Semantic Error: Variable '%s' is undefined.\n", node->name);
			exit(1);
		}
		*total_offset = sym->offset; // Start with the variable's stack offset
		*out_type = sym->type;       // Return its data type context
		return 1;
	}

	if (node->kind == AST_MEMBER_ACCESS) {
		DataType base_type;
		int base_offset = 0;

		// 1. Recursively resolve the left side first
		if (!resolve_member_offset(node->left, &base_offset, &base_type)) {
			return 0;
		}

		// 2. Identify the target structure context name
		char struct_context[64];
		if (node->op == TOKEN_ARROW) {
			if (base_type.kind != TYPE_POINTER || !base_type.base_type || base_type.base_type->kind != TYPE_STRUCT) {
				printf("Semantic Error: Arrow operator '->' used on a non-pointer type.\n");
				exit(1);
			}
			strcpy(struct_context, base_type.base_type->struct_name);
		}
		else { // TOKEN_PERIOD '.'
			if (base_type.kind != TYPE_STRUCT) {
				printf("Semantic Error: Dot operator '.' used on a non-struct type.\n");
				exit(1);
			}
			strcpy(struct_context, base_type.struct_name);
		}

		// 3. Look up the structure definition in the environment
		struct StructEnv* env = lookup_struct(struct_context);
		if (!env) {
			printf("Semantic Error: Structure type '%s' is undefined.\n", struct_context);
			exit(1);
		}

		// 4. Find the matching member inside the structure context
		Member* mem = env->members;
		int member_found = 0;
		while (mem) {
			if (strcmp(mem->name, node->name) == 0) {
				// If it's a pointer arrow target, the base_offset register contains a memory pointer,
				// otherwise it's a stack-allocated variable displacement.
				if (node->op == TOKEN_ARROW) {
					// Arrow handling involves pointer address computation, handled via registers in codegen
					*total_offset = mem->offset;
				}
				else {
					*total_offset = base_offset + mem->offset;
				}
				*out_type = mem->type;
				member_found = 1;
				break;
			}
			mem = mem->next;
		}

		if (!member_found) {
			printf("Semantic Error: Member '%s' not found in struct '%s'.\n", node->name, struct_context);
			exit(1);
		}
		return 1;
	}
	return 0;
}
