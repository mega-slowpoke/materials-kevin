#include <iostream>
#include <vector>
#include <deque>
#include "tree.cpp"
using namespace std;


class Traverse {
public:
    vector<int> levelOrder(TreeNode* root)  {

        vector<int> res;
        if (root == nullptr) {
            return res;
        }

        deque<TreeNode*> q;
        q.push_back(root);

        while (!q.empty()) {
            TreeNode* cur = q.front();
            if (cur->left) {
                q.push_back(cur->left);
            }

            if (cur->right) {
                q.push_back(cur->right);
            }

            res.push_back(cur->value);
            q.pop_front();
        }

        return res;
    }
};