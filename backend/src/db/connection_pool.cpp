#include "db/connection_pool.h"
#include "logging/logger.h"

// --- PooledConnection ---

PooledConnection::PooledConnection(ConnectionPool& pool, std::unique_ptr<pqxx::connection> conn)
  : pool_(&pool), conn_(std::move(conn)) {}

PooledConnection::~PooledConnection() {
  if (conn_) {
    pool_->release(std::move(conn_));
  }
}

PooledConnection::PooledConnection(PooledConnection&& other) noexcept
  : pool_(other.pool_), conn_(std::move(other.conn_)) {
  other.pool_ = nullptr;
}

PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
  if (this != &other) {
    if (conn_) pool_->release(std::move(conn_));
    pool_ = other.pool_;
    conn_ = std::move(other.conn_);
    other.pool_ = nullptr;
  }
  return *this;
}

pqxx::connection& PooledConnection::get() {
  return *conn_;
}
pqxx::connection& PooledConnection::operator*() {
  return *conn_;
}
pqxx::connection* PooledConnection::operator->() {
  return conn_.get();
}

// --- ConnectionPool ---

ConnectionPool::ConnectionPool(const std::string& conn_string, int pool_size)
  : conn_string_(conn_string), pool_size_(pool_size) {
  for (int i = 0; i < pool_size_; ++i) {
    connections_.push(create_connection());
  }
  LOG_INFO_N(
    "db", nullptr, "Connection pool initialized (" + std::to_string(pool_size_) + " connections)");
}

PooledConnection ConnectionPool::acquire() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return !connections_.empty(); });

  auto conn = std::move(connections_.front());
  connections_.pop();
  lock.unlock();

  // Validate connection health; reconnect if stale
  if (!conn || !conn->is_open()) {
    try {
      conn = create_connection();
    } catch (const std::exception& e) {
      // Return a slot to the pool to avoid permanent shrinkage, then rethrow
      std::lock_guard<std::mutex> g(mutex_);
      try {
        connections_.push(create_connection());
      } catch (...) {
        LOG_ERROR_N("db", nullptr, "Failed to create replacement connection");
      }
      cv_.notify_one();
      throw;
    }
  }

  return PooledConnection(*this, std::move(conn));
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> conn) {
  // If the connection is broken, replace it with a fresh one
  if (!conn || !conn->is_open()) {
    try {
      conn = create_connection();
    } catch (const std::exception& e) {
      LOG_ERROR_N("db", nullptr, "Failed to create replacement connection on release");
      // Pool will be one connection short until a future release repairs it
      conn = nullptr;
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (conn) {
    connections_.push(std::move(conn));
  }
  cv_.notify_one();
}

int ConnectionPool::available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(connections_.size());
}

size_t ConnectionPool::size_in_use() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t available = connections_.size();
  size_t total = static_cast<size_t>(pool_size_);
  return total >= available ? total - available : 0;
}

std::unique_ptr<pqxx::connection> ConnectionPool::create_connection() {
  return std::make_unique<pqxx::connection>(conn_string_);
}
