#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "pobox.grpc.pb.h"

class PoBoxServiceImpl final : public pobox::PoBox::Service {
  grpc::Status Search(grpc::ServerContext* /*context*/,
                      const pobox::SearchRequest* request,
                      pobox::SearchResponse* /*response*/) override {
    std::cout << "A search request for '" << request->prefix() << "' was received\n";
    return grpc::Status::OK;
  }

  grpc::Status Learn(grpc::ServerContext* /*context*/,
                     const pobox::LearnRequest* request,
                     pobox::LearnResponse* response) override {
    std::cout << "A learn request for candidate_id='" << request->candidate_id() << "' was received\n";
    response->set_success(true);
    return grpc::Status::OK;
  }

  grpc::Status Sync(grpc::ServerContext* /*context*/,
                    const pobox::SyncRequest* /*request*/,
                    pobox::SyncResponse* /*response*/) override {
    std::cout << "A sync request was received\n";
    return grpc::Status::OK;
  }
};

int main() {
  const std::string address = "0.0.0.0:50051";

  PoBoxServiceImpl service;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "PoBox server listening on " << address << "\n";
  server->Wait();
  return 0;
}
