<?php
/*
 * OpenAI Proxy Server and context window for FujiNet AI SAM App
 * 
 * Copyright (c) 2025 Joe Honold, mozzwald <gmail.com>
 * 
 * GPL v3 License
 */


/* Prep the variables */

// OpenAI API key
$API_KEY = "YOUR_API_KEY";
// Default app Token / Key. used as sort of a password to get a unique token for the first time. sent by app
$defaultKey = "YOUR_DEFAULT_TOKEN_TO_MATCH_APP_CONFIG_H";

// Database Variables
$dbhost = "127.0.0.1";
$dbuser = "YOUR_DB_USERNAME";
$dbpass = 'YOUR_DB_PASSWORD';
$dbname = "ai-sam";

// How many messages to keep saved and send to API
$historyLimit = 9;

// Log File
$log_errors = 1; // 1 = yes to log them in a file, 0 = no
$log_file = "invalid.log";

/* Some helper functions */

/** Convert ASCII text to ATASCII
 * 
 */
function convert_atascii($str)
{
    // First convert to ASCII
    $str = convert_ascii($str);

	$newtext = "";
	for ( $pos=0; $pos < strlen($str); $pos ++ )
	{
		$byte = substr($str, $pos);
		if(ord($byte) == 10) // Replace $0A with SPACE
			$newtext .= chr(32);
		else
			$newtext .= chr(ord($byte));
	}
	return $newtext;
}

/**
 * Remove any non-ASCII characters and convert known non-ASCII characters
 * to their ASCII equivalents, if possible.
 *
 * @param string $string
 * @return string $string
 * @author Jay Williams <myd3.com>
 * @license MIT License
 * @link http://gist.github.com/119517
 */
function convert_ascii($string)
{
    // Replace Single Curly Quotes
    $search[]  = chr(226) . chr(128) . chr(152);
    $replace[] = "'";
    $search[]  = chr(226) . chr(128) . chr(153);
    $replace[] = "'";

    // Replace Smart Double Curly Quotes
    $search[]  = chr(226) . chr(128) . chr(156);
    $replace[] = '"';
    $search[]  = chr(226) . chr(128) . chr(157);
    $replace[] = '"';

    // Replace En Dash
    $search[]  = chr(226) . chr(128) . chr(147);
    $replace[] = '--';

    // Replace Em Dash
    $search[]  = chr(226) . chr(128) . chr(148);
    $replace[] = '---';

    // Replace Bullet
    $search[]  = chr(226) . chr(128) . chr(162);
    $replace[] = '*';

    // Replace Middle Dot
    $search[]  = chr(194) . chr(183);
    $replace[] = '*';

    // Replace Ellipsis with three consecutive dots
    $search[]  = chr(226) . chr(128) . chr(166);
    $replace[] = '...';

    // Apply Replacements
    $string    = str_replace($search, $replace, $string);

    // Remove any non-ASCII Characters
    $string    = preg_replace("/[^\x01-\x7F]/", "", $string);

    return $string;
}

// Only accept POST requests
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(["error" => "Method Not Allowed"]);
    exit;
}

// Read JSON input from the Atari program
$inputJSON = file_get_contents("php://input");
$decodedInput = json_decode($inputJSON, true);

// Ensure request contains valid JSON
if (!$decodedInput) {
    http_response_code(400);

    // Log the invalid JSON to $log_file
    if ($log_errors)
        file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Invalid JSON received:\n" . $inputJSON . "\n\n", FILE_APPEND);

    echo json_encode(["error" => "Invalid JSON input"]);
    exit;
}

// Connect to MySQL via PDO
try {
    $dsn = "mysql:host={$dbhost};dbname={$dbname};charset=utf8mb4";
    $pdo = new PDO($dsn, $dbuser, $dbpass, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    ]);
} catch (PDOException $e) {
    http_response_code(400);
    echo json_encode([
        'error' => 'Database error'
    ]);
    exit;
}

// Check if we have a token_id in the request.
// New requests should have the special token "m98982424z" and 'new' variable in json
// If not new, check if token exists in DB and if not send error

// Check if 'new' token exists in JSON and is our "password", if yes we are asking for a new key.
if (
    isset($decodedInput['new']) &&
    $decodedInput['new'] === $defaultKey
) {
    // If sent an old token, delete its history & invalidate it
    if (isset($decodedInput['token_id']) &&
        $decodedInput['token_id'] !== $defaultKey) {
        $oldToken = $decodedInput['token_id'];

        // Delete all messages for the old token
        try {
            $delMsgs = $pdo->prepare("
                DELETE FROM messages
                 WHERE token_id = :token
            ");
            $delMsgs->execute([':token' => $oldToken]);
            if ($log_errors)
                file_put_contents(
                    $log_file,
                    date("[Y-m-d H:i:s]") 
                    . " Deleted messages for Token ID: {$oldToken}\n",
                    FILE_APPEND
                );
        } catch (PDOException $e) {
            if ($log_errors)
                file_put_contents(
                    $log_file,
                    date("[Y-m-d H:i:s]") 
                    . " Failed to delete messages for Token ID: {$oldToken}: "
                    . $e->getMessage() . "\n",
                    FILE_APPEND
                );
        }

        // Remove the old token from the tokens table
        try {
            $delToken = $pdo->prepare("
                DELETE FROM tokens
                 WHERE token_id = :token
            ");
            $delToken->execute([':token' => $oldToken]);
            if ($log_errors)
                file_put_contents(
                    $log_file,
                    date("[Y-m-d H:i:s]") 
                    . " Invalidated old Token ID: {$oldToken}\n",
                    FILE_APPEND
                );
        } catch (PDOException $e) {
            if ($log_errors)
                file_put_contents(
                    $log_file,
                    date("[Y-m-d H:i:s]") 
                    . " Failed to invalidate Token ID: {$oldToken}: "
                    . $e->getMessage() . "\n",
                    FILE_APPEND
                );
        }
    }

    // Generate a unique new token and insert it into tokens table
    do {
        $token = bin2hex(random_bytes(16));  // 32 hex chars
        try {
            $ins = $pdo->prepare("
                INSERT INTO tokens (token_id)
                VALUES (:token)
            ");
            $ins->execute([':token' => $token]);
            // success, no duplicate
            if ($log_errors)
                file_put_contents(
                    $log_file,
                    date("[Y-m-d H:i:s]") 
                    . " Initiated New Token ID: {$token}\n",
                    FILE_APPEND
                );
            break;
        } catch (PDOException $e) {
            // If it's a duplicate key error, try again
            if (isset($e->errorInfo[1]) && $e->errorInfo[1] == 1062) {
                continue;
            }
            // Some other error
            if ($log_errors)
                file_put_contents(
                    $log_file,
                    date("[Y-m-d H:i:s]") 
                    . " Failed to insert new token {$token}: "
                    . $e->getMessage() . "\n",
                    FILE_APPEND
                );
            http_response_code(500);
            echo json_encode(['error' => 'Could not create token']);
            exit;
        }
    } while (true);

    // Return the new token
    http_response_code(200);
    echo json_encode(['token_id' => $token]);
    exit;
} elseif ( isset($decodedInput['token_id']) &&
    is_string($decodedInput['token_id']) &&
    preg_match('/^[0-9a-f]{32}$/i', $decodedInput['token_id']) ) {
    // Existing session, verify it’s in our DB
    $token = $decodedInput['token_id'];
    $stmt = $pdo->prepare('
        SELECT 1
          FROM tokens
         WHERE token_id = :token
         LIMIT 1
    ');
    $stmt->execute([':token' => $token]);
    if (! $stmt->fetchColumn()) {
        http_response_code(400);
        echo json_encode([
            'error' => 'Unknown session. Use NEW command to start a session',
            "token_id" => $token
        ]);
        exit;
    }
} else {
    // Missing or malformed token
    if ($log_errors)
        file_put_contents(
            $log_file,
            date("[Y-m-d H:i:s]") 
            . " Invalid or Malformed Token ID: {$decodedInput['token_id']}: "
            . $e->getMessage() . "\n",
            FILE_APPEND
        );
    http_response_code(400);
    echo json_encode([
        'error' => 'valid token_id is required'
    ]);
    exit;
}

// Check if we have a content message for the chatbot
if (isset($decodedInput['content']) &&
    is_string($decodedInput['content'])) {
    
    // Trim
    $raw = trim($decodedInput['content']);

    // Remove any control characters except newline
    $clean = preg_replace('/[^\r\n\x20-\x7E]/', '', $raw);

    // Collapse multiple whitespace/newlines to single space or newline
    $clean = preg_replace([ "/[ \t]{2,}/", "/\r?\n{2,}/" ], [ ' ', "\n" ], $clean);

    // Enforce a reasonable length limit
    $maxLen = 2048;
    if (strlen($clean) > $maxLen) {
        $clean = substr($clean, 0, $maxLen);
    }

    // Reject if empty after cleaning
    if ($clean === '') {
        http_response_code(400);
        header('Content-Type: application/json');
        echo json_encode([
            'error'    => 'Content is empty or contains only invalid characters',
            'token_id' => $token
        ]);
        exit;
    }

    $userMessage = $clean;
} else {
    http_response_code(400);
    header('Content-Type: application/json');
    echo json_encode(["error" => "No content received", "token_id" => $token ]);
}

// Get messages from the database if there is a match for the token id and prepare the json

// Fetch up to the last N messages for this token_id
$stmt = $pdo->prepare("
    SELECT role, content
        FROM messages
        WHERE token_id = :token_id
    ORDER BY created_at DESC
        LIMIT :limit
");
$stmt->bindValue(':token_id', $token, PDO::PARAM_STR);
$stmt->bindValue(':limit',    $historyLimit, PDO::PARAM_INT);
$stmt->execute();
$rows = $stmt->fetchAll();  // returns [] if none

// Reverse to chronological order (or leave empty if none)
$history = [];
if (!empty($rows)) {
    $history = array_reverse($rows);
}

// Build the messages array
$systemContent = 
    "You are S.A.M. a text-to-speech assistant running on a FujiNet device. "
    . "You may talk about any topic the end user wishes within your normal constraints. "
    . "Your response must return 2 fields: text_display and text_sam. "
    . "Rules for BOTH text_display and text_sam: "
    . "Do NOT use any special formatting, characters, quotation marks, forward or back slashes, "
    . "special symbols, or escape sequences. Use periods, question or exclamation marks to end sentences. "
    . "Do NOT respond with Unicode characters. "
    . "Rules only applying to text_display: numbers must be printed as digits, use ASCII newlines when needed, "
    . "limit the text_display response to 960 characters or less. "
    . "Rules only applying to text_sam: numbers must be written as words";

$messages = [
    [
        'role'    => 'system',
        'content' => $systemContent
    ]
];

// Append any existing history (if this is not a brand-new chat)
foreach ($history as $turn) {
    $messages[] = [
        'role'    => $turn['role'],   // "user" or "assistant"
        'content' => $turn['content']
    ];
}

// Append the new user message
$messages[] = [
    'role'    => 'user',
    'content' => $userMessage
];

// Define the function schema
$functions = [[
    'name'        => 'vintage_computers_response',
    'description' => 'Provides two text fields for the user',
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
]];

// Assemble full payload
$payload = [
    'model'                  => 'o4-mini-2025-04-16', // gpt-4o-2024-11-20
    'messages'               => $messages,
    'functions'              => $functions,
    'function_call'          => ['name' => 'vintage_computers_response']/*,
    'max_completion_tokens'  => 512,
    'reasoning_effort'       => "low" */
];

// JSON-encode for sending with curl or HTTP client:
$jsonPayload = json_encode($payload, JSON_UNESCAPED_SLASHES|JSON_UNESCAPED_UNICODE);

// Save json to log for testing
//if ($log_errors)
//    file_put_contents($log_file, date("[Y-m-d H:i:s]") . " OpenAI Request JSON:\n" . $jsonPayload . "\n\n", FILE_APPEND);

// Setup OpenAI API request
$url = "https://api.openai.com/v1/chat/completions";
$options = [
    "http" => [
        "header"  => "Authorization: Bearer $API_KEY\r\n" .
                     "Content-Type: application/json\r\n",
        "method"  => "POST",
        "content" => $jsonPayload,
    ]
];

// Send request to OpenAI
$context  = stream_context_create($options);
$response = file_get_contents($url, false, $context);

$response_data = json_decode($response, true);

// Log the response for testing
//if ($log_errors)
//    file_put_contents($log_file, date("[Y-m-d H:i:s]") . " OpenAI Response:\n" . $response . "\n\n", FILE_APPEND);

// Insert token back into the response
if (is_array($response_data)) {
    $response_data['token_id'] = $token;
} else {
    // If the response isn't valid JSON, wrap it in an error with the token
    if ($log_errors)
        file_put_contents($log_file, date("[Y-m-d H:i:s]") . " OpenAI Invalid:\n" . $response . "\n\n", FILE_APPEND);

    http_response_code(400);
    header('Content-Type: application/json');
    echo json_encode(["error" => "Invalid openai response", "token_id" => $token ]);
    exit;
}

// Check for a valid OpenAI function_call
$choice = $response_data['choices'][0]['message'] ?? null;
if (
    $choice !== null &&
    isset($choice['function_call']['arguments']) &&
    is_string($choice['function_call']['arguments'])
) {
    $args = json_decode($choice['function_call']['arguments'], true);
    if (json_last_error() === JSON_ERROR_NONE && isset($args['text_display'])) {

        // Convert text response to atascii
        $args['text_display'] = convert_atascii($args['text_display']);

        // Re-encode and store back into the original response_data
        $response_data['choices'][0]['message']['function_call']['arguments'] =
            json_encode($args, JSON_UNESCAPED_SLASHES|JSON_UNESCAPED_UNICODE);
        $aiMessage = $args['text_display'];

        $minimal = [
            'token_id'     => $token,
            'text_display' => $args['text_display'],
            'text_sam'     => $args['text_sam']
/*
            'choices'  => [ // Full response output
                [
                    'index' => 0,
                    'message' => [
                        'function_call' => [
                            'arguments' => $response_data['choices'][0]['message']['function_call']['arguments']
                        ]
                    ]
                ]
            ]
*/
        ];

        try {
            $pdo->beginTransaction();

            // 1) Save the new user message
            $insertUser = $pdo->prepare("
                INSERT INTO messages (token_id, role, content)
                VALUES (:token, 'user', :content)
            ");
            $insertUser->execute([
                ':token'   => $token,
                ':content' => $userMessage
            ]);

            // 2) Save the AI’s reply (text_display only)
            $insertAI = $pdo->prepare("
                INSERT INTO messages (token_id, role, content)
                VALUES (:token, 'assistant', :content)
            ");
            $insertAI->execute([
                ':token'   => $token,
                ':content' => $aiMessage
            ]);

            // 3) Prune history if we’ve now exceeded our window
            $countStmt = $pdo->prepare("
                SELECT COUNT(*) 
                  FROM messages 
                 WHERE token_id = :token
            ");
            $countStmt->execute([':token' => $token]);
            $total = (int)$countStmt->fetchColumn();

            if ($total > $historyLimit) {
                $deleteOldestBoth = $pdo->prepare("
                    WITH to_delete AS (
                        (SELECT id
                           FROM messages
                          WHERE token_id = :token
                            AND role = 'user'
                       ORDER BY created_at ASC
                          LIMIT 1)
                      UNION ALL
                        (SELECT id
                           FROM messages
                          WHERE token_id = :token
                            AND role = 'assistant'
                       ORDER BY created_at ASC
                          LIMIT 1)
                    )
                    DELETE FROM messages
                     WHERE id IN (SELECT id FROM to_delete)
                ");
                $deleteOldestBoth->execute([':token' => $token]);
            }            

            $pdo->commit();

        } catch (PDOException $e) {
            $pdo->rollBack();
            if ($log_errors)
                file_put_contents(
                    $log_file,
                    date("[Y-m-d H:i:s]") ." Database save error: ". $e->getMessage() ."\n",
                    FILE_APPEND
                );
            http_response_code(500);
            echo json_encode(['error' => 'Database error']);
            exit;
        }

    } else {
        // malformed arguments
        if ($log_errors)
            file_put_contents(
                $log_file,
                date("[Y-m-d H:i:s]") ." OpenAI text_display missing:\n{$response}\n\n",
                FILE_APPEND
            );
        http_response_code(500);
        echo json_encode(['error' => 'Invalid response format from OpenAI.']);
        exit;
    }
} else {
    // no function_call
    if ($log_errors)
        file_put_contents(
            $log_file,
            date("[Y-m-d H:i:s]") ." OpenAI function_call missing:\n{$response}\n\n",
            FILE_APPEND
        );
    http_response_code(500);
    echo json_encode(['error' => 'Unexpected response format from OpenAI.']);
    exit;
}

//if ($log_errors)
//    file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Full OpenAI Response:\n" . json_encode($response_data) . "\n\n", FILE_APPEND);

header('Content-Type: application/json');
echo json_encode($minimal);
if ($log_errors)
    file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Minimal Response:\n" . json_encode($minimal) . "\n\n", FILE_APPEND);
?>
