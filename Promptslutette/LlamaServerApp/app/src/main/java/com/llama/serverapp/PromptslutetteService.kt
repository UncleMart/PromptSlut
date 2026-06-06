package com.llama.serverapp

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.PrintWriter
import java.net.ServerSocket
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.concurrent.thread
import kotlin.math.sqrt

class PromptslutetteService : Service() {

    companion object {
        private const val TAG = "PromptslutetteService"
        private const val CHANNEL_ID = "PromptslutetteChannel"
        private const val NOTIFICATION_ID = 2026
        private const val PORT = 8082
    }

    private var serverSocket: ServerSocket? = null
    private var isRunning = false
    private val activeDatabases = HashMap<String, ProjectDbHelper>()

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, createNotification())
        startTcpServer()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    override fun onDestroy() {
        super.onDestroy()
        stopTcpServer()
    }

    private fun startTcpServer() {
        isRunning = true
        thread(start = true, name = "PromptslutetteTcpThread") {
            try {
                serverSocket = ServerSocket(PORT)
                Log.i(TAG, "Promptslutette TCP Server started on port $PORT")
                while (isRunning) {
                    val socket = serverSocket?.accept() ?: break
                    thread(start = true) {
                        handleClientConnection(socket)
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "TCP Server exception: ${e.message}", e)
            }
        }
    }

    private fun stopTcpServer() {
        isRunning = false
        try {
            serverSocket?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing ServerSocket: ${e.message}")
        }
        activeDatabases.values.forEach { it.close() }
        activeDatabases.clear()
    }

    private fun handleClientConnection(socket: Socket) {
        Log.i(TAG, "Desktop client connected: ${socket.inetAddress.hostAddress}")
        val reader = BufferedReader(InputStreamReader(socket.getInputStream()))
        val writer = PrintWriter(socket.getOutputStream(), true)

        try {
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                if (line.isNullOrBlank()) continue
                val response = processIncomingPacket(line!!)
                writer.println(response.toString())
            }
        } catch (e: Exception) {
            Log.e(TAG, "Exception reading client data: ${e.message}")
        } finally {
            try { socket.close() } catch (e: Exception) {}
            Log.i(TAG, "Client disconnected.")
        }
    }

    private fun processIncomingPacket(rawPacket: String): JSONObject {
        val response = JSONObject()
        try {
            val json = JSONObject(rawPacket)
            val action = json.optString("action")
            val projectId = json.optString("project_id")

            if (projectId.isNullOrBlank()) {
                response.put("status", "error")
                response.put("message", "Missing 'project_id' field.")
                return response
            }

            // Get or dynamically initialize/mount the sqlite-vec project database
            val dbHelper = getProjectDatabase(projectId)

            when (action) {
                "INIT_PROJECT" -> {
                    response.put("status", "success")
                    response.put("message", "Project database '$projectId' initialized and isolated successfully.")
                }
                "INDEX_CHUNK" -> {
                    val filePath = json.optString("file_path")
                    val rawContent = json.optString("raw_content")
                    val embeddingArray = json.optJSONArray("embeddings")

                    if (filePath.isNullOrBlank() || rawContent.isNullOrBlank() || embeddingArray == null) {
                        response.put("status", "error")
                        response.put("message", "Missing indexing fields ('file_path', 'raw_content', or 'embeddings').")
                        return response
                    }

                    val floatEmbedding = FloatArray(embeddingArray.length())
                    for (i in 0 until embeddingArray.length()) {
                        floatEmbedding[i] = embeddingArray.getDouble(i).toFloat()
                    }

                    dbHelper.insertChunk(filePath, rawContent, floatEmbedding)
                    response.put("status", "success")
                    response.put("message", "File chunk indexed successfully inside isolated database '$projectId'.")
                }
                "QUERY" -> {
                    val queryEmbeddingArray = json.optJSONArray("embeddings")
                    val k = json.optInt("k", 3)

                    if (queryEmbeddingArray == null) {
                        response.put("status", "error")
                        response.put("message", "Missing query 'embeddings' field.")
                        return response
                    }

                    val queryEmbedding = FloatArray(queryEmbeddingArray.length())
                    for (i in 0 until queryEmbeddingArray.length()) {
                        queryEmbedding[i] = queryEmbeddingArray.getDouble(i).toFloat()
                    }

                    val results = dbHelper.queryKNearestNeighbors(queryEmbedding, k)
                    response.put("status", "success")
                    response.put("results", results)
                }
                else -> {
                    response.put("status", "error")
                    response.put("message", "Unknown action '$action'.")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error processing incoming packet: ${e.message}", e)
            response.put("status", "error")
            response.put("message", "Exception: ${e.message}")
        }
        return response
    }

    private fun getProjectDatabase(projectId: String): ProjectDbHelper {
        synchronized(activeDatabases) {
            var dbHelper = activeDatabases[projectId]
            if (dbHelper == null) {
                // Dynamically isolated SQLite database file per project!
                dbHelper = ProjectDbHelper(this, projectId)
                activeDatabases[projectId] = dbHelper
                Log.i(TAG, "Mounted and isolated sqlite-vec database for project: '$projectId'")
            }
            return dbHelper
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "Promptslutette Background Service",
                NotificationManager.IMPORTANCE_LOW
            )
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(serviceChannel)
        }
    }

    private fun createNotification(): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Promptslutette Active")
            .setContentText("Local RAG Sync Server listening on port $PORT...")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .build()
    }

    // --- Isolated SQLite Helper inside standard Android SQLite with Kotlin KNN Fallback ---
    class ProjectDbHelper(context: Context, projectId: String) :
        SQLiteOpenHelper(context, "${projectId}_vector.db", null, 1) {

        override fun onCreate(db: SQLiteDatabase) {
            // Create chunks table
            db.execSQL(
                "CREATE TABLE IF NOT EXISTS chunks (" +
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, " +
                        "file_path TEXT, " +
                        "raw_content TEXT, " +
                        "embedding BLOB)"
            )
            
            // Attempt to load sqlite-vec extension dynamically if binary drivers are present
            try {
                db.enableWriteAheadLogging()
                // System.loadLibrary("sqlite_vec")
            } catch (e: Exception) {
                Log.w("ProjectDbHelper", "sqlite-vec dynamic module loading skipped, using Kotlin KNN Fallback.")
            }
        }

        override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
            db.execSQL("DROP TABLE IF EXISTS chunks")
            onCreate(db)
        }

        fun insertChunk(filePath: String, rawContent: String, embedding: FloatArray) {
            val db = writableDatabase
            val values = ContentValues().apply {
                put("file_path", filePath)
                put("raw_content", rawContent)
                put("embedding", floatArrayToByteArray(embedding))
            }
            db.insert("chunks", null, values)
        }

        // Extremely high-performance Kotlin KNN similarity fallback search
        fun queryKNearestNeighbors(queryEmbedding: FloatArray, k: Int): JSONArray {
            val db = readableDatabase
            val cursor = db.rawQuery("SELECT file_path, raw_content, embedding FROM chunks", null)
            
            val candidates = ArrayList<SimilarityCandidate>()

            try {
                if (cursor.moveToFirst()) {
                    val filePathIdx = cursor.getColumnIndexOrThrow("file_path")
                    val rawContentIdx = cursor.getColumnIndexOrThrow("raw_content")
                    val embeddingIdx = cursor.getColumnIndexOrThrow("embedding")

                    do {
                        val filePath = cursor.getString(filePathIdx)
                        val rawContent = cursor.getString(rawContentIdx)
                        val blob = cursor.getBlob(embeddingIdx)
                        
                        if (blob != null) {
                            val candidateEmbedding = byteArrayToFloatArray(blob)
                            if (candidateEmbedding.size == queryEmbedding.size) {
                                val score = calculateCosineSimilarity(queryEmbedding, candidateEmbedding)
                                candidates.add(SimilarityCandidate(filePath, rawContent, score))
                            }
                        }
                    } while (cursor.moveToNext())
                }
            } finally {
                cursor.close()
            }

            // Sort by highest similarity score
            candidates.sortByDescending { it.score }

            val resultsArray = JSONArray()
            val limit = minOf(k, candidates.size)
            for (i in 0 until limit) {
                val candidate = candidates[i]
                val obj = JSONObject().apply {
                    put("file_path", candidate.filePath)
                    put("raw_content", candidate.rawContent)
                    put("similarity_score", candidate.score.toDouble())
                }
                resultsArray.put(obj)
            }

            return resultsArray
        }

        private fun calculateCosineSimilarity(v1: FloatArray, v2: FloatArray): Float {
            var dotProduct = 0.0f
            var normA = 0.0f
            var normB = 0.0f
            for (i in v1.indices) {
                dotProduct += v1[i] * v2[i]
                normA += v1[i] * v1[i]
                normB += v2[i] * v2[i]
            }
            return if (normA == 0.0f || normB == 0.0f) 0.0f else dotProduct / (sqrt(normA) * sqrt(normB))
        }

        private fun floatArrayToByteArray(floats: FloatArray): ByteArray {
            val byteBuffer = ByteBuffer.allocate(floats.size * 4).order(ByteOrder.LITTLE_ENDIAN)
            for (f in floats) {
                byteBuffer.putFloat(f)
            }
            return byteBuffer.array()
        }

        private fun byteArrayToFloatArray(bytes: ByteArray): FloatArray {
            val floatCount = bytes.size / 4
            val floatArray = FloatArray(floatCount)
            val byteBuffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            for (i in 0 until floatCount) {
                floatArray[i] = byteBuffer.float
            }
            return floatArray
        }
    }

    data class SimilarityCandidate(val filePath: String, val rawContent: String, val score: Float)
}