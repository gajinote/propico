#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "pobox.grpc.pb.h"

class PoBoxClient {
 public:
  explicit PoBoxClient(std::shared_ptr<grpc::Channel> channel)
      : stub_(pobox::PoBox::NewStub(channel)) {}

  void Search(const std::string& prefix) {
    pobox::SearchRequest request;
    request.set_prefix(prefix);

    pobox::SearchResponse response;
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
    pobox::LearnRequest request;
    request.set_candidate_id(candidate_id);
    request.set_prefix(prefix);

    pobox::LearnResponse response;
    grpc::ClientContext context;

    const grpc::Status status = stub_->Learn(&context, request, &response);
    if (!status.ok()) {
      std::cerr << "Learn RPC failed: " << status.error_message() << "\n";
      return;
    }

    if (response.success()) {
      std::cout << "Learned: " << candidate_id << " (prefix: " << prefix << ")\n";
    } else {
      std::cerr << "Learn failed\n";
    }
  }

  void Sync(std::int64_t last_sync_timestamp) {
    pobox::SyncRequest request;
    request.set_last_sync_timestamp(last_sync_timestamp);

    pobox::SyncResponse response;
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
  std::unique_ptr<pobox::PoBox::Stub> stub_;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " search <prefix>\n"
              << "       " << argv[0] << " learn <candidate_id> <prefix>\n"
              << "       " << argv[0] << " sync [<last_sync_timestamp>]\n";
    return 1;
  }

  const std::string command = argv[1];
  PoBoxClient client(
      grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));

  if (command == "search" && argc >= 3) {
    client.Search(argv[2]);
  } else if (command == "learn" && argc >= 4) {
    client.Learn(argv[2], argv[3]);
  } else if (command == "sync") {
    const std::int64_t last_sync_ts = (argc >= 3) ? std::stoll(argv[2]) : 0;
    client.Sync(last_sync_ts);
  } else {
    std::cerr << "Invalid command or arguments\n";
    return 1;
  }
  return 0;
}
