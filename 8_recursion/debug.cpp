#include <iostream>
#include <vector>
#include <math.h>

using namespace std;


class Solution {
public:
    
    vector<vector<int>> subsets(vector<int>& nums) {
        vector<int> path;
        vector<vector<int>> res;
        backtracking(1, nums, path, res);
        return res;
    }
    void backtracking(int start, vector<int>& nums, vector<int>& path, vector<vector<int>>& res) {
        res.push_back(path);
        if (res.size() == pow(2, nums.size())) {
            return;
        }
        for (int i = start; i <= nums.size(); i++) {
            path.push_back(nums[i-1]);
            backtracking(i + 1, nums, path, res);
            path.pop_back();
        }
    }
};


int main() {
    vector<int> x = {1,2,3};
    Solution s;
    vector<vector<int>> res = s.subsets(x);
}