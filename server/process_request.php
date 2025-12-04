<?php
/*
 * OpenAI Proxy Server with context window and web search for FujiNet AI SAM App
 * 
 * Copyright (c) 2025 Joe Honold, mozzwald <gmail.com>
 * 
 * GPL v3 License
 * ------------- process_request.php
 * - Loads history for the same token (excluding this assistant row)
 * - Builds OpenAI messages with special assistant JSON handling
 * - Implements a tool loop supporting web_search and get_time
 * - Requires the assistant to finish via compose_reply(text_display, text_sam)
 * - Writes only the final JSON object back into the existing assistant row
 */

include_once "includes.php";

$searchCount = 0;
$maxSearches = 2;

$id = $argv[1] ?? null;
if (!$id) {
    if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Missing message ID argument\n", FILE_APPEND);
    exit;
}

// DB connect
try {
    $dsn = "mysql:host={$dbhost};dbname={$dbname};charset=utf8mb4";
    $pdo = new PDO($dsn, $dbuser, $dbpass, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    ]);
} catch (PDOException $e) {
    if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " DB connect error: {$e->getMessage()}\n", FILE_APPEND);
    exit;
}

// Look up the pending assistant message and its token
$stmt = $pdo->prepare("SELECT token_id FROM messages WHERE id = ? AND role = 'assistant'");
$stmt->execute([$id]);
$row = $stmt->fetch();
if (!$row) {
    if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Invalid or non-assistant message id: $id\n", FILE_APPEND);
    exit;
}
$token_id = $row['token_id'];

/* ---------- System role content (exact per spec) ---------- */
$systemContent =
"You are SAM, a text-to-speech assistant running on a FujiNet device with access to limited tools.

TOOLS YOU CAN USE:
1) web_search — for retrieving current or factual information from the web.
2) get_time   — for retrieving the current UTC time (you convert to the user's timezone if they ask).

TO CALL A TOOL:
When (and only when) you need to use a tool, respond with a single line JSON object and NO extra text:
{\"action\":\"web_search\",\"query\":\"SEARCH TERMS\"}
or
{\"action\":\"get_time\"}

CONSTRAINTS:
- You may perform at most " . $maxSearches . " web_search actions per single user request.
- Prefer to *not* use web search if you already have information about the request.
- After using a tool, read the tool result (which the system will add) and continue the conversation normally.
- Prefer concise, direct answers suitable for display on an Atari 8-bit screen.
- You may talk about any topic the end user wishes within your normal constraints
- Your response must return 2 fields: text_display and text_sam
- Rules for BOTH text_display and text_sam: 
  - Do NOT use any special formatting, characters, quotation marks, forward or back slashes, special symbols, or escape sequences
  - Use periods, question or exclamation marks to end sentences
  - Do NOT respond with Unicode characters
- Rules only applying to text_display
  - numbers must be printed as digits
  - use ASCII newlines when needed
  - limit the text_display response to 960 characters or less
- Rules only applying to text_sam
  - numbers must be written as words
  - phonetic representation of the textual reply for speech output

WHEN YOU ARE FINISHED:
Call the function \"compose_reply\" with the final text_display and text_sam.";

/* ---------- Load most recent $historyLimit history, excluding this assistant row ---------- */
$stmt = $pdo->prepare(
    "SELECT role, content
       FROM (
             SELECT role, content, created_at, id
               FROM messages
              WHERE token_id = :token
                AND id <> :current_id
           ORDER BY created_at DESC, id DESC
              LIMIT :limit_rows
            ) AS recent
      ORDER BY created_at ASC, id ASC"
);
$stmt->bindValue(':token', $token_id, PDO::PARAM_STR);
$stmt->bindValue(':current_id', $id, PDO::PARAM_INT);
$stmt->bindValue(':limit_rows', (int)$historyLimit, PDO::PARAM_INT);
$stmt->execute();
$history = $stmt->fetchAll();

$messages = [
    [
        'role'    => 'system',
        'content' => $systemContent
    ]
];

// Append existing (non-empty) turns
foreach ($history as $turn) {
    $role = $turn['role'];
    $c = trim((string)$turn['content']);
    if ($c === '') continue;

    // If assistant message content is JSON, extract only text_display
    if ($role === 'assistant') {
        $decoded = json_decode($c, true);
        if (json_last_error() === JSON_ERROR_NONE && isset($decoded['text_display'])) {
            $c = $decoded['text_display'];
        }
    }

    $messages[] = [
        'role'    => $role,
        'content' => $c
    ];
}

/* ---------- Functions schema: compose_reply(text_display, text_sam) ---------- */
$functions = [
    [
        'name'        => 'web_search',
        'description' => 'Perform a web search using a query string',
        'parameters'  => [
            'type'       => 'object',
            'properties' => [
                'query' => [
                    'type'        => 'string',
                    'description' => 'Search query to look up'
                ]
            ],
            'required' => ['query']
        ]
    ],
    [
        'name'        => 'get_time',
        'description' => 'Get the current UTC time',
        'parameters'  => [
            'type'       => 'object',
            'properties' => new stdClass(),
            'required'   => []
        ]
    ],
    [
        'name'        => 'compose_reply',
        'description' => 'Finish by providing two text fields for the user',
        'parameters'  => [
            'type'       => 'object',
            'properties' => [
                'text_display' => [
                    'type'        => 'string',
                    'description' => 'Human-readable output limited to 960 characters'
                ],
                'text_sam'     => [
                    'type'        => 'string',
                    'description' => 'Phonetic version of the text_display string for SAM'
                ]
            ],
            'required' => ['text_display','text_sam']
        ]
    ]
];

/* ---------- Helpers ---------- */
function call_openai_chat($API_KEY, $payload, $log_errors = 0, $log_file = 'invalid.log') {
    $ch = curl_init("https://api.openai.com/v1/chat/completions");
    if ($log_errors && $debug) {
        // Log full outbound JSON payload
        $toSend = json_encode($payload);
        file_put_contents($log_file, date("[Y-m-d H:i:s]") . " OpenAI request =>\n  " . $toSend . "\n", FILE_APPEND);
    }
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_POST           => true,
        CURLOPT_HTTPHEADER     => [
            "Content-Type: application/json",
            "Authorization: Bearer $API_KEY"
        ],
        CURLOPT_POSTFIELDS     => json_encode($payload),
        CURLOPT_TIMEOUT        => 120
    ]);
    $response   = curl_exec($ch);
    $curl_error = curl_error($ch);
    curl_close($ch);
    if ($log_errors && $debug) {
        // Log full raw response body
        $respToLog = (is_string($response) ? $response : json_encode($response));
        file_put_contents($log_file, date("[Y-m-d H:i:s]") . " OpenAI response <=\n  " . $respToLog . "\n", FILE_APPEND);
    }
    if ($curl_error) {
        if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " cURL error: $curl_error\n", FILE_APPEND);
        return [null, $curl_error];
    }
    $data = json_decode($response, true);
    if (!is_array($data)) {
        if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Invalid JSON from OpenAI\n" . $response . "\n\n", FILE_APPEND);
        return [null, 'Invalid JSON response'];
    }
    return [$data, null];
}

function search_web_via_openai($API_KEY, $query, $log_errors = 0, $log_file = 'invalid.log') {
    $messages = [
        ['role' => 'system', 'content' => 'Perform a focused web retrieval and return a short factual summary. Include dates when relevant.'],
        ['role' => 'user',   'content' => (string)$query],
    ];
    $payload = [
        'model'    => 'gpt-4o-mini-search-preview',
        'messages' => $messages,
    ];
    [$data, $err] = call_openai_chat($API_KEY, $payload, $log_errors, $log_file);
    if ($err || !isset($data['choices'][0]['message']['content'])) {
        return 'No results found.';
    }
    if ($log_errors) {
        // Log websearch
        file_put_contents($log_file, date("[Y-m-d H:i:s]") . " OpenAI Web Search (token_id ".$token_id."):\n  {" . $query . "}\n", FILE_APPEND);
    }

    return trim((string)$data['choices'][0]['message']['content']);
}

function get_current_utc() {
    return gmdate('Y-m-d H:i:s') . ' UTC';
}

function parse_tool_json_if_valid($text) {
    if (!is_string($text)) return null;
    if (strpos($text, "\n") !== false) return null; // must be single line
    $trim = trim($text);
    if ($trim === '') return null;
    if ($trim[0] !== '{' || substr($trim, -1) !== '}') return null;
    $obj = json_decode($trim, true);
    if (!is_array($obj)) return null;
    if (!isset($obj['action'])) return null;
    $action = $obj['action'];
    if ($action !== 'web_search' && $action !== 'get_time') return null;
    if ($action === 'web_search' && !isset($obj['query'])) return null;
    if ($action === 'web_search' && !is_string($obj['query'])) return null;
    return ['action' => $action, 'query' => $obj['query'] ?? null, 'raw' => $trim];
}

/**
 * Prune oldest messages for a token so only the most recent $historyLimit remain
 */
function prune_msgs($pdo, $token_id, $historyLimit, $log_errors = 0, $log_file = 'invalid.log') {
    try {
        $stmt = $pdo->prepare("SELECT id FROM messages WHERE token_id = ? ORDER BY created_at ASC, id ASC");
        $stmt->execute([$token_id]);
        $ids = $stmt->fetchAll(PDO::FETCH_COLUMN, 0);
        $total = is_array($ids) ? count($ids) : 0;
        $excess = $total - (int)$historyLimit;
        if ($excess > 0) {
            $toDelete = array_slice($ids, 0, $excess);
            $placeholders = implode(',', array_fill(0, count($toDelete), '?'));
            $del = $pdo->prepare("DELETE FROM messages WHERE id IN ($placeholders)");
            $del->execute($toDelete);
            if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Pruned " . count($toDelete) . " old messages for token $token_id
", FILE_APPEND);
        }
    } catch (Throwable $e) {
        if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " prune_msgs error: " . $e->getMessage() . "
", FILE_APPEND);
    }
}


/* ---------- Tool loop ---------- */
$loopSafety = 0;
while (true) {
    $loopSafety++;
    if ($loopSafety > 12) {
        $messages[] = [
            'role'    => 'system',
            'content' => 'Finish by calling compose_reply with valid text_display and text_sam as per the rules.'
        ];
    }

    $payload = [
//        'model'         => 'o4-mini-2025-04-16',
		'model'			=> 'gpt-5-mini',
        'messages'      => $messages,
        'functions'     => $functions,
        'function_call' => 'auto',
    ];

    [$response_data, $err] = call_openai_chat($API_KEY, $payload, $log_errors, $log_file);
    if ($err || !$response_data) {
        $fallback = json_encode(['text_display' => "Error: $err", 'text_sam' => 'Error']);
        $stmt = $pdo->prepare("UPDATE messages SET content=?, status=0 WHERE id=?");
        $stmt->execute([$fallback, $id]);
        prune_msgs($pdo, $token_id, $historyLimit, $log_errors ?? 0, $log_file ?? 'invalid.log');
        exit;
    }

    $choice = $response_data['choices'][0]['message'] ?? [];

    // Handle function calls first (tools or finalization)
    if (isset($choice['function_call']['name'])) {
        $fcName = $choice['function_call']['name'];
        $fcArgs = $choice['function_call']['arguments'] ?? '';
        $args = [];
        if (is_string($fcArgs)) {
            $tmp = json_decode($fcArgs, true);
            if (is_array($tmp)) $args = $tmp;
        }

        if ($fcName === 'web_search') {
            $query = isset($args['query']) && is_string($args['query']) ? $args['query'] : '';
            $toolJson = json_encode(['action' => 'web_search', 'query' => $query]);
            $searchCount++;
            if ($searchCount > $maxSearches) {
                $messages[] = ['role' => 'assistant', 'content' => $toolJson];
                $messages[] = ['role' => 'system', 'content' => 'Search limit reached. Answer using what you already know.'];
                continue;
            }
            $result = search_web_via_openai($API_KEY, (string)$query, $log_errors, $log_file);
            $messages[] = ['role' => 'assistant', 'content' => $toolJson];
            $messages[] = ['role' => 'system', 'content' => 'Search result: ' . $result];
            continue;
        } elseif ($fcName === 'get_time') {
            $toolJson = json_encode(['action' => 'get_time']);
            $utc = get_current_utc();
            $messages[] = ['role' => 'assistant', 'content' => $toolJson];
            $messages[] = ['role' => 'system', 'content' => 'Current UTC time: ' . $utc];
            continue;
        } elseif ($fcName === 'compose_reply') {
            // Finalize
            $argsJson = $fcArgs;
            $replyArr = ['text_display' => 'Error: no arguments', 'text_sam' => 'Error'];
            $usedContentFallback = false;
            if (is_string($argsJson)) {
                $decoded = json_decode($argsJson, true);
                if (is_array($decoded)) {
                    $display = isset($decoded['text_display']) ? convert_ascii($decoded['text_display']) : '';
                    $sam     = isset($decoded['text_sam'])     ? convert_ascii($decoded['text_sam'])     : '';
                    if ($display === '' || $sam === '') {
                        $usedContentFallback = true;
                    } else {
                        if (strlen($display) > 960) $display = substr($display, 0, 960);
                        $replyArr = ['text_display' => $display, 'text_sam' => $sam];
                    }
                } else {
                    $usedContentFallback = true;
                }
            } else {
                $usedContentFallback = true;
            }

            if ($usedContentFallback) {
                $content = isset($choice['content']) ? convert_ascii((string)$choice['content']) : '';
                if ($content === '') $content = 'Done.';
                if (strlen($content) > 960) $content = substr($content, 0, 960);
                $replyArr = ['text_display' => $content, 'text_sam' => $content];
            }

            $toStore = json_encode($replyArr);
            $stmt = $pdo->prepare("UPDATE messages SET content=?, status=0 WHERE id=?");
            $stmt->execute([$toStore, $id]);
            if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Minimal Response:\n  " . json_encode($replyArr) . "\n", FILE_APPEND);
            prune_msgs($pdo, $token_id, $historyLimit, $log_errors ?? 0, $log_file ?? 'invalid.log');
            exit;
        } else {
            // Unknown function, fall back to content path
        }
    }
// Otherwise, see if it's a tool JSON
    $content = isset($choice['content']) ? (string)$choice['content'] : '';
    $tool = parse_tool_json_if_valid($content);
    if ($tool) {
        if ($tool['action'] === 'web_search') {
            $searchCount++;
            if ($searchCount > $maxSearches) {
                $messages[] = ['role' => 'assistant', 'content' => $tool['raw']];
                $messages[] = ['role' => 'system', 'content' => 'Search limit reached. Answer using what you already know.'];
                continue;
            }
            $result = search_web_via_openai($API_KEY, (string)$tool['query'], $log_errors, $log_file);
            $messages[] = ['role' => 'assistant', 'content' => $tool['raw']];
            $messages[] = ['role' => 'system', 'content' => 'Search result: ' . $result];
            continue;
        } elseif ($tool['action'] === 'get_time') {
            $utc = get_current_utc();
            $messages[] = ['role' => 'assistant', 'content' => $tool['raw']];
            $messages[] = ['role' => 'system', 'content' => 'Current UTC time: ' . $utc];
            continue;
        }
    }

    // Neither function_call nor tool JSON: append content and nudge
    if ($content !== '') {
        $messages[] = ['role' => 'assistant', 'content' => $content];
    }
    $messages[] = [
        'role'    => 'system',
        'content' => 'Finish by calling compose_reply with valid text_display and text_sam as per the rules.'
    ];
}

// Should never reach here
exit;
?>
