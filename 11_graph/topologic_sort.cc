#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
using namespace std;

class Solution {
public:
vector<int> findOrder(int numCourses, vector<vector<int>>& prerequisites) {
        // 建图
        vector<int> res;
        vector<vector<int>> graph(numCourses);
        vector<int> indegree(numCourses, 0);
        // vector<bool> visited(numCourses, 0);
        // for (int i = 0; i < numCourses; i++) {
        for (auto pre : prerequisites) {
            int from = pre[1];
            int to = pre[0];
            graph[from].push_back(to);
            indegree[to]++;
        }


        // 拓扑排序
        // 从入度为0的节点开始，把该节点周围所有节点的入度都-1
        // 如果有新的入度为0的节点，也把这个节点加入q
        deque<int> q;
        for (int i = 0; i < numCourses; i++) {
            if (indegree[i] == 0) {
                q.push_back(i);
                res.push_back(i);
            }
        }

        while (!q.empty()) {
            int cur = q.front();
            q.pop_front();

            for (int neighbor : graph[cur]) {
                indegree[neighbor] -= 1;
                if (indegree[neighbor] == 0) {
                    q.push_back(neighbor);
                    res.push_back(neighbor);
                }
            }
        }

        if (res.size() == numCourses) {
            return res;
        } else {
            return {};
        }

    }
};