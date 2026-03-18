#pragma once
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <queue>
#include <string>

class ConnectionPool;

// RAII guard: checks out a connection on construction, returns it on destruction.
class PooledConnection {
public:
  PooledConnection(ConnectionPool& pool, std::unique_ptr<pqxx::connection> conn);
  ~PooledConnection();

  PooledConnection(const PooledConnection&) = delete;
  PooledConnection& operator=(const PooledConnection&) = delete;
  PooledConnection(PooledConnection&& other) noexcept;
  PooledConnection& operator=(PooledConnection&& other) noexcept;

  pqxx::connection& get();
  pqxx::connection& operator*();
  pqxx::connection* operator->();

private:
  ConnectionPool* pool_;
  std::unique_ptr<pqxx::connection> conn_;
};

class ConnectionPool {
public:
  explicit ConnectionPool(const std::string& conn_string, int pool_size = 10);
  ~ConnectionPool() = default;

  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  PooledConnection acquire();
  void release(std::unique_ptr<pqxx::connection> conn);

  int size() const {
    return pool_size_;
  }
  int available() const;

private:
  std::string conn_string_;
  int pool_size_;
  std::queue<std::unique_ptr<pqxx::connection>> connections_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;

  std::unique_ptr<pqxx::connection> create_connection();
};
