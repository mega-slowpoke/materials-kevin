#include <iostream>
#include <vector>
#include <iostream>
#include <unordered_map>

using namespace std;

class GraphMatrix {
    int n; // 顶点数
    vector<vector<int>> adj; // 邻接矩阵

public:
    GraphMatrix(int ver_num) : n(ver_num), adj(ver_num, vector<int>(ver_num, 0)) {}

    // 无向图
    void addEdge(int u, int v) {
        adj[u][v] = 1;
        adj[v][u] = 1; 
    }

    void printMatrix() {
        cout << "Adjacency Matrix:" << endl;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                cout << adj[i][j] << " ";
            }
            cout << endl;
        }
    }
};


class GraphList {
    int n; // 顶点数
    unordered_map<int, vector<int>> adj; // 邻接表

public:
    GraphList(int n) : n(n), adj(n) {}

    void addEdge(int u, int v) {
        adj[u].push_back(v);
        adj[v].push_back(u); // 如果是有向图，去掉这一行
    }

    void printList() {
        cout << "Adjacency List:" << endl;
        for (int i = 0; i < n; i++) {
            cout << i << ": ";
            for (int v : adj[i]) cout << v << " ";
            cout << endl;
        }
    }

    // === DFS ===
    void dfsUtil(int u, vector<bool>& visited) {
        visited[u] = true;
        // cout << u << " ";
        for (int v : adj[u]) {
            if (!visited[v]) {
                dfsUtil(v, visited);
            }
        }
    }

    void DFS(int start) {
        vector<bool> visited(n, false);
        cout << "DFS starting from " << start << ": ";
        dfsUtil(start, visited);
        cout << endl;
    }

    // === BFS ===
    void BFS(int start) {
        vector<bool> visited(n, false);
        vector<int> queue;
        visited[start] = true;
        queue.push_back(start);

        cout << "BFS starting from " << start << ": ";

        while (!queue.empty()) {
            int u = queue.front();
            queue.erase(queue.begin());
            cout << u << " ";

            for (int v : adj[u]) {
                if (!visited[v]) {
                    visited[v] = true;
                    queue.push_back(v);
                }
            }
        }
        cout << endl;
    }
};



int main() {
    int n = 5; // 顶点数 (0 ~ 4)

    // 邻接矩阵
    GraphMatrix gm(n);
    gm.addEdge(0, 1);
    gm.addEdge(0, 4);
    gm.addEdge(1, 2);
    gm.addEdge(1, 3);
    gm.addEdge(1, 4);
    gm.printMatrix();
    cout << endl;

    // 邻接表
    GraphList gl(n);
    gl.addEdge(0, 1);
    gl.addEdge(0, 4);
    gl.addEdge(1, 2);
    gl.addEdge(1, 3);
    gl.addEdge(1, 4);
    gl.printList();

    // 遍历
    gl.DFS(0);
    gl.BFS(0);

    return 0;
}
