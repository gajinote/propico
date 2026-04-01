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

 private:
  std::unique_ptr<pobox::PoBox::Stub> stub_;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <prefix>\n";
    return 1;
  }

  const std::string prefix = argv[1];
  PoBoxClient client(
      grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));

  client.Search(prefix);
  return 0;
}
