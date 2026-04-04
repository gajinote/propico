#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include "pobox.grpc.pb.h"

#include "dictionary.h"
#include "scorer.h"

static constexpr std::size_t kDefaultMaxCandidates = 10;

class PoBoxServiceImpl final : public pobox::PoBox::Service {
public:
    explicit PoBoxServiceImpl(pobox::engine::Dictionary dict)
        : dict_(std::move(dict)), scorer_() {}

    grpc::Status Search(grpc::ServerContext* /*context*/,
                        const pobox::SearchRequest* request,
                        pobox::SearchResponse* response) override {
        const auto& prefix = request->prefix();
        std::cout << "A search request for '" << prefix << "' was received\n";

        const std::size_t limit = request->max_candidates() != 0
                                      ? request->max_candidates()
                                      : kDefaultMaxCandidates;

        auto candidates = dict_.search(prefix, limit * 2);
        candidates      = scorer_.apply(std::move(candidates), boost_map_);

        // 件数を limit に絞る
        if (candidates.size() > limit) candidates.resize(limit);

        for (const auto& c : candidates) {
            auto* proto_c = response->add_candidates();
            proto_c->set_id(c.id);
            proto_c->set_text(c.text);
            proto_c->set_reading(c.reading);
            proto_c->set_score(c.final_score());
        }
        return grpc::Status::OK;
    }

    grpc::Status Learn(grpc::ServerContext* /*context*/,
                       const pobox::LearnRequest* request,
                       pobox::LearnResponse* response) override {
        const auto& id = request->candidate_id();
        std::cout << "A learn request for candidate_id='" << id << "' was received\n";
        ++boost_map_[id];
        response->set_success(true);
        return grpc::Status::OK;
    }

    grpc::Status Sync(grpc::ServerContext* /*context*/,
                      const pobox::SyncRequest* /*request*/,
                      pobox::SyncResponse* /*response*/) override {
        std::cout << "A sync request was received\n";
        return grpc::Status::OK;
    }

private:
    pobox::engine::Dictionary dict_;
    pobox::engine::Scorer     scorer_;
    // Phase 3 で SQLite に置き換えるまでのインメモリ boost カウンタ
    std::unordered_map<std::string, int> boost_map_;
};

int main(int argc, char* argv[]) {
    const std::string dict_path = (argc >= 2) ? argv[1] : "dict/default.tsv";
    const std::string address   = "0.0.0.0:50051";

    pobox::engine::Dictionary dict;
    try {
        dict.load(dict_path);
        std::cout << "Dictionary loaded: " << dict.size() << " entries from "
                  << dict_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[warn] " << e.what() << " — starting with empty dictionary\n";
    }

    PoBoxServiceImpl service(std::move(dict));
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "PoBox server listening on " << address << "\n";
    server->Wait();
    return 0;
}
