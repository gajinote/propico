#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "propico.grpc.pb.h"

class PropicoClient {
 public:
  explicit PropicoClient(std::shared_ptr<grpc::Channel> channel)
      : stub_(propico::Propico::NewStub(channel)) {}

  void Search(const std::string& prefix) {
    propico::SearchRequest request;
    request.set_prefix(prefix);

    propico::SearchResponse response;
    grpc::ClientContext context;

    const grpc::Status status = stub_->Search(&context, request, &response);
    if (!status.ok()) {
      std::cerr << "Search RPC failed: " << status.error_message() << "\n";
      return;
    }

    if (response.candidates().empty()) {
      std::cout << "(no candidates)\n";
      return;
    }

    for (const auto& candidate : response.candidates()) {
      std::cout << candidate.text()
                << "  [" << candidate.reading() << "]"
                << "  score=" << candidate.score() << "\n";
    }
  }

  void Learn(const std::string& candidate_id, const std::string& prefix) {
    propico::LearnRequest request;
    request.set_candidate_id(candidate_id);
    request.set_prefix(prefix);

    propico::LearnResponse response;
    grpc::ClientContext context;

    const grpc::Status status = stub_->Learn(&context, request, &response);
    if (!status.ok()) {
      std::cerr << "Learn RPC failed: " << status.error_message() << "\n";
      return;
    }

    if (response.success()) {
      std::cout << "Learned: " << candidate_id << " (prefix: " << prefix << ")\n";
    } else {
      std::cerr << "Learn failed: " << response.error_message() << "\n";
    }
  }

  void Sync(std::int64_t last_sync_timestamp) {
    propico::SyncRequest request;
    request.set_last_sync_timestamp(last_sync_timestamp);

    propico::SyncResponse response;
    grpc::ClientContext context;

    const grpc::Status status = stub_->Sync(&context, request, &response);
    if (!status.ok()) {
      std::cerr << "Sync RPC failed: " << status.error_message() << "\n";
      return;
    }

    std::cout << "server_timestamp=" << response.server_timestamp() << "\n";
    std::cout << "entries=" << response.entries_size() << "\n";
    for (const auto& entry : response.entries()) {
      std::cout << entry.candidate_id()
                << "  count=" << entry.count()
                << "  updated_at=" << entry.updated_at() << "\n";
    }
  }

 private:
  std::unique_ptr<propico::Propico::Stub> stub_;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " search <prefix>\n"
              << "       " << argv[0] << " learn <candidate_id> <prefix>\n"
              << "       " << argv[0] << " sync [<last_sync_timestamp>]\n";
    return 1;
  }

  const std::string command = argv[1];
  const char* server_env = std::getenv("PROPICO_SERVER");
  const std::string server_addr = server_env ? server_env : "localhost:50051";
  PropicoClient client(
      grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials()));

  if (command == "search" && argc >= 3) {
    client.Search(argv[2]);
  } else if (command == "learn" && argc >= 4) {
    client.Learn(argv[2], argv[3]);
  } else if (command == "sync") {
    std::int64_t last_sync_ts = 0;
    if (argc >= 3) {
      try {
        last_sync_ts = std::stoll(argv[2]);
      } catch (const std::exception&) {
        std::cerr << "Invalid timestamp: " << argv[2] << "\n";
        return 1;
      }
    }
    client.Sync(last_sync_ts);
  } else {
    std::cerr << "Invalid command or arguments\n";
    return 1;
  }
  return 0;
}
