#include "../../../test.h"
#include "../../../../../lib/common/balancer/roundrobin.c"

static h2o_socketpool_target_vector_t gen_targets(size_t size) {
    size_t i;
    h2o_socketpool_target_vector_t targets = {};
    
    h2o_vector_reserve(NULL, &targets, size);
    for (i = 0; i < size; i++) {
        h2o_socketpool_target_t *target = h2o_mem_alloc(sizeof(*target));
        target->_shared.leased_count = 0;
        target->conf.weight = 1;
        targets.entries[i] = target;
    }
    targets.size = size;
    
    return targets;
}

static void free_targets(h2o_socketpool_target_vector_t *targets)
{
    size_t i;
    
    for (i = 0; i < targets->size; i++) {
        free(targets->entries[i]);
    }
    
    free(targets->entries);
}

static void test_when_backend_down(void)
{
    h2o_socketpool_target_vector_t targets = gen_targets(10);
    int tried[10] = {};
    size_t i;
    size_t selected;
    void *balancer;
    
    construct(&targets, NULL, &balancer);
    
    for (i = 0; i < 10; i++) {
        selected = selector(&targets, balancer, tried, NULL);
        ok(selected >= 0 && selected < 10);
        ok(!tried[selected]);
        tried[selected] = 1;
    }
    
    finalize(balancer);
    
    free_targets(&targets);
}

static int check_if_acceptable(h2o_socketpool_target_vector_t *targets, size_t selected, size_t seqnum)
{
    size_t total_weight = 0;
    size_t min_seqnum = 0;
    size_t max_seqnum;
    size_t i;
    
    for (i = 0; i < targets->size; i++)
        total_weight += targets->entries[i]->conf.weight;
    
    for (i = 0; i < selected; i++)
        min_seqnum += targets->entries[i]->conf.weight;
    max_seqnum = min_seqnum + targets->entries[selected]->conf.weight;
    
    seqnum %= total_weight;
    if (seqnum < min_seqnum || seqnum >= max_seqnum) {
        return -1;
    }
    return 0;
}

static void test_round_robin(void)
{
    h2o_socketpool_target_vector_t targets = gen_targets(10);
    size_t i, selected;
    int tried[10] = {};
    int check_result = 1;
    void *balancer;
    
    construct(&targets, NULL, &balancer);
    
    for (i = 0; i < 10000; i++) {
        selected = selector(&targets, balancer, tried, NULL);
        if (selected > 10) {
            ok(selected >= 0 && selected < 10);
            goto Done;
        }
        check_result = check_if_acceptable(&targets, selected, i);
        if (check_result == -1) {
            ok(!check_result);
            goto Done;
        }
        targets.entries[selected]->_shared.leased_count++;
    }
    ok(!check_result);
    
Done:
    finalize(balancer);
    free_targets(&targets);
}

static void test_round_robin_weighted(void)
{
    h2o_socketpool_target_vector_t targets = gen_targets(10);
    size_t i, selected;
    int tried[10] = {};
    int check_result = 1;
    void *balancer;
    
    for (i = 0; i < 10; i++)
        targets.entries[i]->conf.weight = i % 3 + 1;
    
    construct(&targets, NULL, &balancer);
    
    for (i = 0; i < 10000; i++) {
        selected = selector(&targets, balancer, tried, NULL);
        if (selected > 10) {
            ok(selected >= 0 && selected < 10);
            goto Done;
        }
        check_result = check_if_acceptable(&targets, selected, i);
        if (check_result == -1) {
            ok(!check_result);
            goto Done;
        }
        targets.entries[selected]->_shared.leased_count++;
    }
    ok(!check_result);
    
Done:
    finalize(balancer);
    free_targets(&targets);
}

void test_lib__common__balancer__roundrobin_c(void)
{
    subtest("when_backend_down", test_when_backend_down);
    subtest("round_robin", test_round_robin);
    subtest("round_robin_weighted", test_round_robin_weighted);
}

