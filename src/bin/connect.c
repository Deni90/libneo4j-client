/* vi:set ts=4 sw=4 expandtab:
 *
 * Copyright 2016, Chris Leishman (http://github.com/cleishm)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "../../config.h"
#include "connect.h"
#include "authentication.h"
#include <assert.h>
#include <errno.h>


static int parse_host_port(shell_state_t *state, const char *s,
        char *hostname, size_t hostname_len, unsigned int *port);
static int update_password_and_reconnect(shell_state_t *state);


int db_connect(shell_state_t *state, const char *connect_string)
{
    if (state->session != NULL && db_disconnect(state))
    {
        return -1;
    }
    assert(state->session == NULL);

    neo4j_connection_t *connection;

    char hostname[NEO4J_MAXHOSTLEN];
    unsigned int port;
    int r = parse_host_port(state, connect_string, hostname,
            sizeof(hostname), &port);
    if (r < 0)
    {
        return -1;
    }
    if (r == 0)
    {
        connection = neo4j_tcp_connect(hostname, port, state->config,
                state->connect_flags);
    }
    else
    {
        connection = neo4j_connect(connect_string, state->config,
                state->connect_flags);
    }

    if (connection == NULL)
    {
        switch (errno)
        {
        case NEO4J_NO_SERVER_TLS_SUPPORT:
            fprintf(state->err, "connection failed: A secure"
                    " connection could not be esablished (try --insecure)\n");
            break;
        case NEO4J_INVALID_URI:
            fprintf(state->err, "invalid URL '%s'\n", connect_string);
            break;
        default:
            neo4j_perror(state->err, errno, "connection failed");
            break;
        }
        return -1;
    }

    neo4j_session_t *session = neo4j_new_session(connection);
    if (session == NULL)
    {
        neo4j_perror(state->err, errno, "connection failed");
        neo4j_end_session(session);
        neo4j_close(connection);
        return -1;
    }

    state->connection = connection;
    state->session = session;

    if (state->password_prompt && neo4j_credentials_expired(session) &&
            update_password_and_reconnect(state))
    {
        assert(state->connection == NULL);
        return -1;
    }

    return 0;
}


int parse_host_port(shell_state_t *state, const char *s,
        char *hostname, size_t hostname_len, unsigned int *port)
{
    size_t hlen = strcspn(s, "/:");
    const char *host_end = s + hlen;
    if (*host_end == '/')
    {
        return 1;
    }
    if (hlen >= hostname_len)
    {
        fprintf(state->err, "hostname is too long\n");
        return -1;
    }

    if (*host_end == '\0')
    {
        *port = 0;
    }
    else if (*(host_end + 1) == '/')
    {
        return 1;
    }
    else if (*(host_end + 1) == '\0')
    {
        fprintf(state->err, "Invalid URL '%s' "
                "(you may need to put quotes around the whole URL)\n", s);
        return -1;
    }
    else
    {
        assert(*host_end == ':');
        char *port_end;
        long p = strtol(host_end+1, &port_end, 10);
        if (*port_end != '\0' || p <= 0 || p > UINT16_MAX)
        {
            fprintf(state->err, "invalid port '%s'\n", host_end+1);
            return -1;
        }
        *port = (unsigned int)p;
    }

    memcpy(hostname, s, hlen);
    hostname[hlen] = '\0';
    return 0;
}


int update_password_and_reconnect(shell_state_t *state)
{
    neo4j_connection_t *connection = state->connection;
    neo4j_session_t *session = state->session;
    state->connection = NULL;
    state->session = NULL;

    neo4j_config_t *config = NULL;

    char *hostname = strdup(neo4j_connection_hostname(connection));
    if (hostname == NULL)
    {
        goto failure;
    }

    unsigned int port = neo4j_connection_port(connection);

    const char *username = neo4j_connection_username(connection);
    if (username == NULL)
    {
        fprintf(state->err, "connection failed: "
                "credentials have expired, yet no username was provided.\n");
        goto failure_cleanup;
    }

    config = neo4j_config_dup(state->config);
    if (config == NULL)
    {
        goto failure;
    }

    assert(state->tty != NULL);
    fprintf(state->tty,
            "The current password has expired and must be changed.\n");
    char password[NEO4J_MAXPASSWORDLEN];
    if (change_password(state, session, password, sizeof(password)))
    {
        goto failure_cleanup;
    }

    if (neo4j_config_set_username(config, username))
    {
        goto failure;
    }

    if (neo4j_config_set_password(config, password))
    {
        goto failure;
    }

    neo4j_end_session(session);
    session = NULL;
    neo4j_close(connection);

    connection = neo4j_tcp_connect(hostname, port, config,
            state->connect_flags);
    if (connection == NULL)
    {
        goto failure;
    }

    session = neo4j_new_session(connection);
    if (session == NULL)
    {
        goto failure;
    }

    state->connection = connection;
    state->session = session;
    free(hostname);
    neo4j_config_free(config);
    return 0;

failure:
    neo4j_perror(state->err, errno, "connection failed");
failure_cleanup:
    if (session != NULL)
    {
        neo4j_end_session(session);
    }
    if (connection != NULL)
    {
        neo4j_close(connection);
    }
    free(hostname);
    neo4j_config_free(config);
    return -1;
}


int db_disconnect(shell_state_t *state)
{
    if (state->session == NULL)
    {
        fprintf(state->err, "ERROR: not connected\n");
        return -1;
    }
    neo4j_end_session(state->session);
    state->session = NULL;
    neo4j_close(state->connection);
    state->connection = NULL;
    return 0;
}
