#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
using namespace std;

class Solution {
public:
    int numIslands(vector<vector<char>>& grid) {
        int m = grid.size();
        int n = grid[0].size();
        vector<vector<bool>> visited(m, vector<bool>(n, false));
        int count = 0;
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < n; j++) {
                if (grid[i][j] == '1' && visited[i][j] == false) {
                    bfs(visited, i, j, grid);
                    count++;
                }
            }
        }

        return count;
    }

 
    void bfs(vector<vector<bool>>& visited, int startX, int startY, vector<vector<char>>& grid) {
        int m = grid.size();
        int n = grid[0].size();
        deque<vector<int>> q;
        q.push_back({startX, startY});
        visited[startX][startY] = true;

        while (!q.empty()) {
            // 取出queue中的头元素
            vector<int> cur = q.front();
            q.pop_front();
            int curX = cur[0];
            int curY = cur[1];

            // 对该元素的上下左右做bfs
            if (curX - 1 >= 0 && grid[curX-1][curY] == '1' && !visited[curX-1][curY]) {
                q.push_back({curX-1, curY});
                visited[curX-1][curY] = true;    
            }

            if (curX + 1 < m && grid[curX+1][curY] == '1' && !visited[curX+1][curY]) {
                q.push_back({curX+1, curY});
                visited[curX+1][curY] = true;    
            }

            if (curY - 1 >= 0 && grid[curX][curY-1] == '1' && !visited[curX][curY-1]) {
                q.push_back({curX, curY-1});
                visited[curX][curY-1] = true;    
            }

             if (curY+1 < n && grid[curX][curY+1] == '1' && !visited[curX][curY+1]) {
                q.push_back({curX, curY+1});
                visited[curX][curY+1] = true;    
            }
        }
    }



    // // 把当前这个岛屿中所有1都标为visited
    // void dfs(vector<vector<bool>>& visited, int x, int y, vector<vector<char>>& grid) {
    //     // // boundary check
    //     int m = grid.size();
    //     int n = grid[0].size();
    //     if (x < 0 || x >= m || y < 0 || y >= n) {
    //         return;
    //     }

    //     // base case
    //     if (grid[x][y] == '0' || visited[x][y] == true) {
    //         return;
    //     }

    //     // recursive step
    //     visited[x][y] = true;

    //     // 左
    //     dfs(visited, x, y-1, grid);
    //     // 右
    //     dfs(visited, x, y+1, grid);
    //     // 上
    //     dfs(visited, x-1, y, grid);
    //     // 下
    //     dfs(visited, x+1, y, grid);
    // }


    bool isBipartite(vector<vector<int>>& graph) {
        int n = graph.size();
        vector<int> color(n, -1); // -1 = 未染色, 0 = 红, 1 = 蓝

        for (int start = 0; start < n; ++start) {
            if (color[start] != -1) continue; // 已经染色过了

            deque<int> q;
            q.push_back(start);
            color[start] = 0; // 给起点染成红色

            while (!q.empty()) {
                int cur = q.front(); 
                q.pop_front();
                for (int next : graph[cur]) {
                    if (color[next] == -1) {
                        color[next] = 1 - color[cur]; // 相反颜色
                        q.push_back(next);
                    } else if (color[next] == color[cur]) {
                        return false; // 相邻的颜色相同 -> 不是二分图
                    }
                }
            }
        }

        return true;
    }
};