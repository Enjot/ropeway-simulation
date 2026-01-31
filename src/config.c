#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void config_set_defaults(Config *cfg) {
    cfg->station_capacity = 50;
    cfg->simulation_duration_real = 120;  // 2 minutes real time
    cfg->sim_start_hour = 8;
    cfg->sim_start_minute = 0;
    cfg->sim_end_hour = 17;
    cfg->sim_end_minute = 0;
    cfg->chair_travel_time_sim = 5;  // 5 sim minutes per ride

    cfg->tourist_spawn_rate = 5;
    cfg->max_concurrent_tourists = 100;
    cfg->max_tracked_tourists = 5000;

    cfg->vip_percentage = 1;
    cfg->walker_percentage = 50;

    cfg->trail_walk_time = 30;
    cfg->trail_bike_fast_time = 15;
    cfg->trail_bike_medium_time = 25;
    cfg->trail_bike_slow_time = 40;

    cfg->ticket_t1_duration = 60;   // 1 sim hour
    cfg->ticket_t2_duration = 120;  // 2 sim hours
    cfg->ticket_t3_duration = 180;  // 3 sim hours
}

int config_load(const char *path, Config *cfg) {
    // Set defaults first
    config_set_defaults(cfg);

    FILE *f = fopen(path, "r");
    if (!f) {
        perror("config_load: fopen");
        return -1;
    }

    char line[256];
    int line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        // Skip empty lines and comments
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
            len--;
        }
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
        }

        // Parse key=value
        char key[64] = {0};
        char value[64] = {0};

        if (sscanf(line, "%63[^=]=%63s", key, value) != 2) {
            fprintf(stderr, "config_load: invalid format at line %d: %s\n", line_num, line);
            continue;
        }

        // Match keys
        if (strcmp(key, "STATION_CAPACITY") == 0) {
            cfg->station_capacity = atoi(value);
        } else if (strcmp(key, "SIMULATION_DURATION_REAL_SECONDS") == 0) {
            cfg->simulation_duration_real = atoi(value);
        } else if (strcmp(key, "SIM_START_HOUR") == 0) {
            cfg->sim_start_hour = atoi(value);
        } else if (strcmp(key, "SIM_START_MINUTE") == 0) {
            cfg->sim_start_minute = atoi(value);
        } else if (strcmp(key, "SIM_END_HOUR") == 0) {
            cfg->sim_end_hour = atoi(value);
        } else if (strcmp(key, "SIM_END_MINUTE") == 0) {
            cfg->sim_end_minute = atoi(value);
        } else if (strcmp(key, "CHAIR_TRAVEL_TIME_SIM_MINUTES") == 0) {
            cfg->chair_travel_time_sim = atoi(value);
        } else if (strcmp(key, "TOURIST_SPAWN_RATE_PER_SECOND") == 0) {
            cfg->tourist_spawn_rate = atoi(value);
        } else if (strcmp(key, "MAX_CONCURRENT_TOURISTS") == 0) {
            cfg->max_concurrent_tourists = atoi(value);
        } else if (strcmp(key, "MAX_TRACKED_TOURISTS") == 0) {
            cfg->max_tracked_tourists = atoi(value);
        } else if (strcmp(key, "VIP_PERCENTAGE") == 0) {
            cfg->vip_percentage = atoi(value);
        } else if (strcmp(key, "WALKER_PERCENTAGE") == 0) {
            cfg->walker_percentage = atoi(value);
        } else if (strcmp(key, "TRAIL_WALK_TIME_SIM_MINUTES") == 0) {
            cfg->trail_walk_time = atoi(value);
        } else if (strcmp(key, "TRAIL_BIKE_FAST_TIME_SIM_MINUTES") == 0) {
            cfg->trail_bike_fast_time = atoi(value);
        } else if (strcmp(key, "TRAIL_BIKE_MEDIUM_TIME_SIM_MINUTES") == 0) {
            cfg->trail_bike_medium_time = atoi(value);
        } else if (strcmp(key, "TRAIL_BIKE_SLOW_TIME_SIM_MINUTES") == 0) {
            cfg->trail_bike_slow_time = atoi(value);
        } else if (strcmp(key, "TICKET_T1_DURATION_SIM_MINUTES") == 0) {
            cfg->ticket_t1_duration = atoi(value);
        } else if (strcmp(key, "TICKET_T2_DURATION_SIM_MINUTES") == 0) {
            cfg->ticket_t2_duration = atoi(value);
        } else if (strcmp(key, "TICKET_T3_DURATION_SIM_MINUTES") == 0) {
            cfg->ticket_t3_duration = atoi(value);
        } else {
            fprintf(stderr, "config_load: unknown key at line %d: %s\n", line_num, key);
        }
    }

    fclose(f);
    return 0;
}

int config_validate(const Config *cfg) {
    int valid = 1;

    if (cfg->station_capacity <= 0) {
        fprintf(stderr, "config: STATION_CAPACITY must be > 0\n");
        valid = 0;
    }

    if (cfg->simulation_duration_real <= 0) {
        fprintf(stderr, "config: SIMULATION_DURATION_REAL_SECONDS must be > 0\n");
        valid = 0;
    }

    if (cfg->sim_start_hour < 0 || cfg->sim_start_hour > 23) {
        fprintf(stderr, "config: SIM_START_HOUR must be 0-23\n");
        valid = 0;
    }

    if (cfg->sim_end_hour < 0 || cfg->sim_end_hour > 23) {
        fprintf(stderr, "config: SIM_END_HOUR must be 0-23\n");
        valid = 0;
    }

    // Issue #13 fix: Add minute field validation (0-59)
    if (cfg->sim_start_minute < 0 || cfg->sim_start_minute > 59) {
        fprintf(stderr, "config: SIM_START_MINUTE must be 0-59\n");
        valid = 0;
    }

    if (cfg->sim_end_minute < 0 || cfg->sim_end_minute > 59) {
        fprintf(stderr, "config: SIM_END_MINUTE must be 0-59\n");
        valid = 0;
    }

    int start_minutes = cfg->sim_start_hour * 60 + cfg->sim_start_minute;
    int end_minutes = cfg->sim_end_hour * 60 + cfg->sim_end_minute;

    if (end_minutes <= start_minutes) {
        fprintf(stderr, "config: end time must be after start time\n");
        valid = 0;
    }

    if (cfg->tourist_spawn_rate <= 0) {
        fprintf(stderr, "config: TOURIST_SPAWN_RATE_PER_SECOND must be > 0\n");
        valid = 0;
    }

    if (cfg->max_concurrent_tourists <= 0) {
        fprintf(stderr, "config: MAX_CONCURRENT_TOURISTS must be > 0\n");
        valid = 0;
    }

    if (cfg->max_tracked_tourists <= 0) {
        fprintf(stderr, "config: MAX_TRACKED_TOURISTS must be > 0\n");
        valid = 0;
    }

    if (cfg->vip_percentage < 0 || cfg->vip_percentage > 100) {
        fprintf(stderr, "config: VIP_PERCENTAGE must be 0-100\n");
        valid = 0;
    }

    if (cfg->walker_percentage < 0 || cfg->walker_percentage > 100) {
        fprintf(stderr, "config: WALKER_PERCENTAGE must be 0-100\n");
        valid = 0;
    }

    if (cfg->chair_travel_time_sim <= 0) {
        fprintf(stderr, "config: CHAIR_TRAVEL_TIME_SIM_MINUTES must be > 0\n");
        valid = 0;
    }

    return valid ? 0 : -1;
}
