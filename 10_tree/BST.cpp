#include "tree.cpp"

class BST {

   
public:
    // 返回插入后的头节点
    TreeNode* insert(TreeNode* root, int new_val) {
        TreeNode* cur = root;
        TreeNode* parent = nullptr;
        while (cur != nullptr) {
            if (new_val < cur->value) {
                parent = cur;
                cur = cur->left;
            } else {
                parent = cur;
                cur = cur->right;
            }
        }

        if (new_val < parent->value) {
            parent->left = new TreeNode(new_val);
        } else {
            parent->right = new TreeNode(new_val);
        }

        return root;
    }

    // 返回插入后的整棵树的头节点
    TreeNode* insertRec(TreeNode* root, TreeNode* new_node) {
        // base case
        if (root == nullptr) {
            return new_node;
        } 
        
        // recursive step
        if (new_node->value < root->value) {
            TreeNode* left = insertRec(root->left, new_node);
            root->left = left;
        } else {
            TreeNode* right = insertRec(root->right, new_node);
            root->right = right;
        }

        return root;
    }


    // 删除remove_val节点，并返回删除该节点后的整棵树的头节点
    TreeNode* remove(TreeNode* root, int remove_val) {
        if (root == nullptr) {
            return nullptr;
        }

        if (remove_val < root->value) {
            root->left = remove(root->left, remove_val);   
        } else if (remove_val > root->value) {
            root->right = remove(root->right, remove_val);
        } else {
            if (root->left == nullptr && root->right == nullptr) {
                delete root;
                root = nullptr;
            } else if (root->left == nullptr && root->right != nullptr) {
                TreeNode* temp = root;
                root = root->right; 
                delete temp;
            } else if (root->left != nullptr && root->right == nullptr){
                TreeNode* temp = root;
                root = root->left; 
                delete temp;
            } else {
                TreeNode* succ = findMin(root->right); 
                root->value = succ->value;
                root->right = remove(root->right, succ->value);
            }
        }

        return root;
    }
};

