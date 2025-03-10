/*
MIT License

Copyright (c) 2023 CovERUshKA

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef ASTAR_H
#define ASTAR_H

#include <include/base.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <unordered_map>
#include <vector>

class AStar
{
public:
	AStar(const std::vector<std::vector<int>> &grid, std::pair<int, int> goal) :
		grid(grid), rows(grid.size()), cols(grid[0].size())
	{
		distance = std::vector<std::vector<double>>(rows, std::vector<double>(cols, std::numeric_limits<double>::infinity()));
		dijkstra(goal.first, goal.second);
	}

	AStar(const std::vector<std::vector<int>> &grid, std::vector<std::pair<int, int>> goals) :
		grid(grid), rows(grid.size()), cols(grid[0].size())
	{
		distance = std::vector<std::vector<double>>(rows, std::vector<double>(cols, std::numeric_limits<double>::infinity()));
		for(size_t i = 0; i < goals.size(); i++)
		{
			dijkstra(goals[i].first, goals[i].second);
		}
	}

	struct Node
	{
		int y, x;
		double dist;

		Node(int y, int x, double dist) :
			x(x), y(y), dist(dist) {}

		double f() const { return dist; }

		bool operator>(const Node &other) const { return f() > other.f(); }
	};

	int distanceToGoal(int Y, int X)
	{
		return distance[Y][X];
	}

	int distanceToGoal(std::pair<int, int> pos)
	{
		return distance[pos.first][pos.second] == 0;
	}

	bool isGoal(int Y, int X)
	{
		return distance[Y][X] == 0;
	}

	bool isGoal(std::pair<int, int> pos)
	{
		return distance[pos.first][pos.second] == 0;
	}

	std::vector<std::pair<int, int>> findPath(std::pair<int, int> start, int max_length = 30)
	{
		auto [sy, sx] = start;

		if(!isValid(sy, sx) && !isDangerous(sy, sx))
			return {}; // Start is invalid
		if(isGoal(start))
			return {};

		std::vector<std::pair<int, int>> ret;
		std::vector<std::pair<int, int>> directions = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
		int y = sy;
		int x = sx;
		for(size_t i = 0; i < max_length; i++)
		{
			std::pair<int, int> bestMove = {-1, -1};
			double minDistance = std::numeric_limits<double>::infinity();
			for(const auto &[dy, dx] : directions)
			{
				int ny = y + dy;
				int nx = x + dx;

				if(isValid(ny, nx) && !isDangerous(ny, nx) && distance[ny][nx] < minDistance)
				{
					minDistance = distance[ny][nx];
					bestMove = {dy, dx};
					if(isDangerous(ny + 1, nx))
						bestMove.first = -1;
				}
			}

			if(bestMove == std::pair<int, int>{-1, -1})
			{
				break; // No valid move found, end the search
			}

			auto [dy, dx] = bestMove;
			y += dy;
			x += dx;
			ret.push_back(bestMove);
			if(isGoal(y, x))
				break;
		}

		// Path found
		return ret;
	}

private:
	const std::vector<std::vector<int>> &grid;
	int rows, cols;
	std::vector<std::vector<double>> distance;

	bool isValid(int y, int x) const
	{
		return y >= 0 && y < rows && x >= 0 && x < cols && grid[y][x] == 0;
	}

	bool isDangerous(int y, int x) const
	{
		return y >= 0 && y < rows - 1 && x >= 0 && x < cols && (grid[y][x] == -1 || grid[y + 1][x] == -1);
	}

	// Run Dijkstra's algorithm from a single source node
	std::vector<std::vector<double>> dijkstra(int goalY, int goalX)
	{
		std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;

		distance[clamp(goalY, 0, rows - 1)][clamp(goalX, 0, cols - 1)] = 0;
		pq.push(Node(clamp(goalY, 0, rows - 1), clamp(goalX, 0, cols - 1), 0));

		std::vector<std::pair<int, int>> directions = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

		while(!pq.empty())
		{
			Node current = pq.top();
			pq.pop();

			int y = current.y;
			int x = current.x;
			double dist = current.dist;

			if(dist > distance[y][x])
				continue;

			for(const auto &[dx, dy] : directions)
			{
				int nx = x + dx;
				int ny = y + dy;

				if(isValid(ny, nx) && !isDangerous(ny, nx) && dist + 1 < distance[ny][nx])
				{
					distance[ny][nx] = dist + 1;
					pq.push(Node(ny, nx, distance[ny][nx]));
				}
			}
		}

		return distance;
	}
};

#endif // ASTAR_H