#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "propico.grpc.pb.h"

#include "dictionary.h"
#include "history.h"
#include "scorer.h"

static constexpr std::size_t kDefaultMaxCandidates = 10;

class PropicoServiceImpl final : public propico::Propico::Service {
public:
    explicit PropicoServiceImpl(propico::engine::Dictionary dict, const std::string& db_path)
        : dict_(std::move(dict)), scorer_(), history_(db_path) {}

    grpc::Status Search(grpc::ServerContext* /*context*/,
                        const propico::SearchRequest* request,
                        propico::SearchResponse* response) override {
        const auto& prefix = request->prefix();
        std::cout << "A search request for '" << prefix << "' was received\n";

        const std::size_t limit = request->max_candidates() != 0
                                      ? request->max_candidates()
                                      : kDefaultMaxCandidates;

        auto candidates = dict_.search(prefix, limit * 2);
        candidates      = scorer_.apply(std::move(candidates), history_.load_boost_map());

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
                       const propico::LearnRequest* request,
                       propico::LearnResponse* response) override {
        const auto& id     = request->candidate_id();
        const auto& prefix = request->prefix();
        std::cout << "A learn request for candidate_id='" << id << "' was received\n";
        try {
            history_.record(id, prefix);
            response->set_success(true);
        } catch (const std::exception& e) {
            response->set_success(false);
            response->set_error_message(e.what());
        }
        return grpc::Status::OK;
    }

    grpc::Status Sync(grpc::ServerContext* /*context*/,
                      const propico::SyncRequest* request,
                      propico::SyncResponse* response) override {
        const auto  last_sync_ts = request->last_sync_timestamp();
        const auto  server_ts    = static_cast<std::int64_t>(std::time(nullptr));
        std::cout << "A sync request was received (last_sync_timestamp=" << last_sync_ts
                  << ")\n";

        auto rows = history_.load_updated_since(last_sync_ts);

        response->set_server_timestamp(server_ts);
        for (const auto& row : rows) {
            auto* entry = response->add_entries();
            entry->set_candidate_id(row.candidate_id);
            entry->set_count(row.count);
            entry->set_updated_at(row.updated_at);
        }

        std::cout << "Sync response: " << rows.size() << " updated entries\n";
        return grpc::Status::OK;
    }

private:
    propico::engine::Dictionary dict_;
    propico::engine::Scorer     scorer_;
    propico::engine::History    history_;
};

int main(int argc, char* argv[]) {
    const std::string dict_path = (argc >= 2) ? argv[1] : "dict/default.tsv";
    const std::string db_path   = (argc >= 3) ? argv[2] : "propico_history.db";
    const std::string address   = "0.0.0.0:50051";

    propico::engine::Dictionary dict;
    try {
        dict.load(dict_path);
        std::cout << "Dictionary loaded: " << dict.size() << " entries from "
                  << dict_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "[warn] " << e.what() << " — starting with empty dictionary\n";
    }

    PropicoServiceImpl service(std::move(dict), db_path);
    std::cout << "History database: " << db_path << "\n";
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Propico server listening on " << address << "\n";
    server->Wait();
    return 0;
}
