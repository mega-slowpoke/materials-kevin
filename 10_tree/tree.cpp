
class TreeNode {
private:
   
public:

    TreeNode* left;
    TreeNode* right;
    int value;  

    TreeNode(int value) {
        this->value = value;
        left = right = nullptr;
    }

    void insertLeft(int val) {
        TreeNode* cur = this;
        
        while (cur->left != nullptr && cur->right != nullptr) {
            cur = cur->left;
        }
        cur->left = new TreeNode(val);
    }

    void insertRight(TreeNode* node) {
        TreeNode* cur = this;
        
        while (cur->left != nullptr && cur->right != nullptr) {
            cur = cur->left;
        }
        cur->left = node;
    }
    
    // remove(7) -> 7这个节点给删掉
    // 怎么去定位
    void remove(int val) {
        // 定位7在哪？
        // a. 如果我们的树是有序的就好了 
        // b. 如果无序，完整地遍历整个树(左+右)


        // 1. 7 是leaf, 1->right = nullptr
        // 2. 7 要么只有左节点，要么只有右节点, 1->right = 7->left/right
        // 3. 7如果同时有左节点和右节点， 7->right.insertLeft(7.left),  1->right = 7->right;
    }

};





