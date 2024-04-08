#ifndef M_PI
#define M_PI 3.14159265358979323846
#define RADIUS 5.0

#endif
#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>
#include <SFML/Network.hpp> 

#include <winsock2.h>
#include <ws2tcpip.h>

#include "imgui.h"
#include "imgui-SFML.h"

#include <vector>
#include <cmath>
#include <random>
#include <iostream>
#include <string>
#include <chrono>
#include <mutex>
#include <queue>
#include <atomic>
#include <thread>
#include <functional>
#include <condition_variable>
#include <utility>
#include <filesystem>
#include <stdexcept>
#include <Windows.h>


#include <ctime>
#include <fstream>
#include <sstream> // for std::stringstream

template<typename T>
const T& clamp(const T& value, const T& min, const T& max) {
    return (value < min) ? min : ((max < value) ? max : value);
}
namespace fs = std::filesystem;

class ThreadPool {
public:
    ThreadPool(size_t numThreads) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            threads.emplace_back([this] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) {
                            return;
                        }
                        task = std::move(tasks.front());
                        tasks.pop();
                    }

                    task();
                }
                });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : threads) {
            worker.join();
        }
    }

    template <class F, class... Args>
    void enqueue(F&& f, Args&&... args) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace([=]() mutable { std::forward<F>(f)(std::forward<Args>(args)...); });
        }
        condition.notify_one();
    }

private:
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
};

class Particle {
    private:
        float x;
        float y;
        sf::Vector2f position;

    public:
        Particle(float posX, float posY) : position(posX, posY) {}

        // Accessor methods for x and y positions
        float getX() const { return x; }
        float getY() const { return y; }
        sf::Vector2f getPosition() const {
            return position;
        }
};

void renderWalls(sf::RenderWindow& window,
    const std::vector<sf::VertexArray>& walls,
    std::mutex& mutex,
    float scale) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& wall : walls) {
        // Create a transformed copy of the wall vertices with the given scale
        sf::VertexArray scaledWall(wall.getPrimitiveType());
        for (size_t i = 0; i < wall.getVertexCount(); ++i) {
            scaledWall.append(sf::Vertex(sf::Vector2f(wall[i].position.x * scale, wall[i].position.y * scale)));
        }
        window.draw(scaledWall);
    }
}

void renderParticles(const std::vector<Particle>& particles,
    sf::RenderWindow& window,
    std::mutex& mutex,
    float scale) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& particle : particles) {
        sf::CircleShape particleShape(5.0f * scale); // Adjust particle size based on scale
        sf::Vector2f particlePosition = particle.getPosition();
        particleShape.setPosition(particlePosition);
        particleShape.setFillColor(sf::Color::Green);
        window.draw(particleShape);
    }
}


float dot(const sf::Vector2f& v1, const sf::Vector2f& v2) {
    return v1.x * v2.x + v1.y * v2.y;
}

float distance(const sf::Vector2f& v1, const sf::Vector2f& v2) {
    return std::sqrt((v1.x - v2.x) * (v1.x - v2.x) + (v1.y - v2.y) * (v1.y - v2.y));
}

sf::Vector2f getClosestPointOnSegment(const sf::Vector2f& point, const sf::Vector2f& segmentStart, const sf::Vector2f& segmentEnd) {
    sf::Vector2f segment = segmentEnd - segmentStart;
    float lengthSquared = segment.x * segment.x + segment.y * segment.y; // Compute squared length directly
    if (lengthSquared == 0) {
        return segmentStart;
    }
    float t = std::max(0.0f, std::min(1.0f, dot(point - segmentStart, segment) / lengthSquared));
    return segmentStart + t * segment;
}

bool collidesWithWalls(const sf::Vector2f& position, const std::vector<sf::VertexArray>& walls, float canvasWidth, float canvasHeight) {
    for (const auto& wall : walls) {
        for (size_t i = 0; i < wall.getVertexCount() - 1; ++i) {
            sf::Vector2f p1 = wall[i].position;
            sf::Vector2f p2 = wall[i + 1].position;
            p1.x -= RADIUS;
            p1.y -= RADIUS;
            p2.x -= RADIUS;
            p2.y -= RADIUS;
            sf::Vector2f closestPoint = getClosestPointOnSegment(position, p1, p2);



            if (distance(position, closestPoint) < RADIUS) {
                return true; 
            }
        }
    }
    if (position.x < 0 || position.x >= canvasWidth || position.y < 0 || position.y >= canvasHeight) {
        return true;
    }
    return false; 
}

void handleInput(const std::string& clientId, sf::CircleShape& ball, float canvasWidth, float canvasHeight, const std::vector<sf::VertexArray>& walls, SOCKET clientSocket, sf::RenderWindow& window) {
    const float speed = 5.0f;
    std::cout << "Client ID: " << clientId << std::endl;

    while (true) {
        if (window.hasFocus()) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::W) && ball.getPosition().y >= 0) {
                sf::Vector2f nextPosition = ball.getPosition();

                nextPosition.y -= speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(0, -speed);
                }

                sf::Vector2f ballPosition = ball.getPosition(); // Get updated ball position
                // Create a string to hold the serialized data
                std::ostringstream oss;

                // Serialize the data into the string
                oss << clientId << " " << ballPosition.x << " " << ballPosition.y;

                // Convert the stringstream to a string
                std::string serializedData = oss.str();

                // Print the serialized data
                std::cout << "Before sending: " << serializedData << std::endl;

                // Send the serialized data to the server (non-blocking)
                const char* data = serializedData.c_str(); // Get pointer to the data
                std::size_t dataSize = serializedData.size(); // Get size of the data

                int bytesSent = send(clientSocket, data, dataSize, 0);
                if (bytesSent == SOCKET_ERROR) {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        std::cerr << "Failed to send position data to the server!" << std::endl;
                    }
                }
                else {
                    std::cout << "Position data sent to server." << std::endl;
                }


            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::A) && ball.getPosition().x >= 0) {
                sf::Vector2f nextPosition = ball.getPosition();
                nextPosition.x -= speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(-speed, 0);
                }

                sf::Vector2f ballPosition = ball.getPosition(); // Get updated ball position
                // Create a string to hold the serialized data
                std::ostringstream oss;

                // Serialize the data into the string
                oss << clientId << " " << ballPosition.x << " " << ballPosition.y;

                // Convert the stringstream to a string
                std::string serializedData = oss.str();

                // Print the serialized data
                std::cout << "Before sending: " << serializedData << std::endl;

                // Send the serialized data to the server (non-blocking)
                const char* data = serializedData.c_str(); // Get pointer to the data
                std::size_t dataSize = serializedData.size(); // Get size of the data

                int bytesSent = send(clientSocket, data, dataSize, 0);
                if (bytesSent == SOCKET_ERROR) {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        std::cerr << "Failed to send position data to the server!" << std::endl;
                    }
                }
                else {
                    std::cout << "Position data sent to server." << std::endl;
                }

            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::S) && ball.getPosition().y + ball.getRadius() + RADIUS < canvasHeight) {
                sf::Vector2f nextPosition = ball.getPosition();
                nextPosition.y += speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(0, speed);
                }

                sf::Vector2f ballPosition = ball.getPosition(); // Get updated ball position
                // Create a string to hold the serialized data
                std::ostringstream oss;

                // Serialize the data into the string
                oss << clientId << " " << ballPosition.x << " " << ballPosition.y;

                // Convert the stringstream to a string
                std::string serializedData = oss.str();

                // Print the serialized data
                std::cout << "Before sending: " << serializedData << std::endl;

                // Send the serialized data to the server (non-blocking)
                const char* data = serializedData.c_str(); // Get pointer to the data
                std::size_t dataSize = serializedData.size(); // Get size of the data

                int bytesSent = send(clientSocket, data, dataSize, 0);
                if (bytesSent == SOCKET_ERROR) {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        std::cerr << "Failed to send position data to the server!" << std::endl;
                    }
                }
                else {
                    std::cout << "Position data sent to server." << std::endl;
                }
            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::D) &&
                ball.getPosition().x + ball.getRadius() + RADIUS < canvasWidth) {
                sf::Vector2f nextPosition = ball.getPosition();
                nextPosition.x += speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(speed, 0);
                }

                sf::Vector2f ballPosition = ball.getPosition(); // Get updated ball position
                // Create a string to hold the serialized data
                std::ostringstream oss;

                // Serialize the data into the string
                oss << clientId << " " << ballPosition.x << " " << ballPosition.y;

                // Convert the stringstream to a string
                std::string serializedData = oss.str();

                // Print the serialized data
                //std::cout << "Before sending: " << serializedData << std::endl;

                // Send the serialized data to the server (non-blocking)
                const char* data = serializedData.c_str(); // Get pointer to the data
                std::size_t dataSize = serializedData.size(); // Get size of the data

                int bytesSent = send(clientSocket, data, dataSize, 0);
                if (bytesSent == SOCKET_ERROR) {
                    if (WSAGetLastError() != WSAEWOULDBLOCK) {
                        std::cerr << "Failed to send position data to the server!" << std::endl;
                    }
                }
                else {
                    std::cout << "Position data sent to server." << std::endl;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Function to receive data from the server
void receiveDataFromServer(SOCKET clientSocket, std::vector<Particle>& particles, std::mutex& mutex) {
    while (true) {
        // Receive particle position
        sf::Vector2f position;
        if (recv(clientSocket, reinterpret_cast<char*>(&position), sizeof(position), 0) == SOCKET_ERROR) {
            std::cerr << "Error receiving particle position." << std::endl;
            break;
        }

        // Lock the mutex before accessing the particles vector
        std::lock_guard<std::mutex> lock(mutex);

        // Create a new particle with the received position and add it to the vector
        particles.emplace_back(position.x, position.y);
    }
}


class IDGenerator {
private:
    int counter;

public:
    IDGenerator() : counter(0) {}

    std::string generateID() {
        counter++;
        return "A" + std::to_string(counter);
    }
};



int main() {
    IDGenerator idGenerator;

    // Initialize WSA variables
    WSADATA wsaData;
    int wsaerr;
    WORD wVersionRequested = MAKEWORD(2, 2);
    wsaerr = WSAStartup(wVersionRequested, &wsaData);
    if (wsaerr != 0) {
        std::cout << "The Winsock dll not found" << std::endl;
        return 0;
    }
    else {
        std::cout << "Client is on" << std::endl << "The Winsock dll found" << std::endl;
        std::cout << "The status: " << wsaData.szSystemStatus << std::endl;
    }

    // Create socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cout << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "Socket is on" << std::endl;
    }

    // Connect to server
    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr("192.168.68.110"); // Change to server IP address
    service.sin_port = htons(55555); // Change to server port
    if (connect(clientSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        std::cout << "Failed to connect." << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "Connected to server." << std::endl;
    }

    std::string id = idGenerator.generateID();
    std::cout << "Generated ID: " << id << std::endl;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    sf::RenderWindow window(sf::VideoMode(1280 + 10, 720 + 10), "Particle Bouncing Application"); // adjusting size for aesthetic purposes, canvas walls are still 1280 x 720
    window.setFramerateLimit(60);

    sf::Clock clock;
    sf::Clock deltaClock;
    sf::Clock fpsClock;
    int frameCount = 0;
    float fps = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();

    ImGui::SFML::Init(window);

    std::vector<Particle> particles;
    float canvasWidth = 1280.0f;
    float canvasHeight = 720.0f;
    float speed = 100.0f;
    float startAngle = 0.0f;
    float endAngle = 180.0f;

    float angle = 45.0f * M_PI / 180.0f; // Convert angle to radians
    int numParticles = 1;
    std::vector<sf::VertexArray> walls;

    bool isDrawingLine = false;
    sf::Vector2f lineStart(100.0f, 360.0f); // Default line start point
    sf::Vector2f lineEnd(1180.0f, 360.0f);  // Default line end point

    sf::Vector2f spawnPoint(640.0f, 360.0f); // Default spawn point

    sf::CircleShape ball(RADIUS);
    bool developerMode = true; // Default to developer mode
    ball.setFillColor(sf::Color::Red);
    ball.setPosition(640, 360); // Initial position

    std::thread inputThread(&handleInput,
        id,
        std::ref(ball),
        canvasWidth,
        canvasHeight,
        std::ref(walls), clientSocket, std::ref(window));

    // Create a packet to send ball position
    sf::Vector2f ballPosition = ball.getPosition();
    // Create a string to hold the serialized data
    std::ostringstream oss;

    // Serialize the data into the string
    oss << id << " " << ballPosition.x << " " << ballPosition.y;

    // Convert the stringstream to a string
    std::string serializedData = oss.str();

    // Print the serialized data
    std::cout << "Before sending: " << serializedData << std::endl;

    // Send the serialized data to the server (non-blocking)
    const char* data = serializedData.c_str(); // Get pointer to the data
    std::size_t dataSize = serializedData.size(); // Get size of the data

    int bytesSent = send(clientSocket, data, dataSize, 0);
    if (bytesSent == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            std::cerr << "Failed to send position data to the server!" << std::endl;
        }
    }
    else {
        std::cout << "Position data sent to server." << std::endl;
    }


    unsigned int numThreads = std::thread::hardware_concurrency();
    ThreadPool threadPool(numThreads);

    // Mutex for synchronization
    std::mutex mutex;

    std::thread receiveThread(receiveDataFromServer, clientSocket, std::ref(particles), std::ref(mutex));

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(event);

            if (event.type == sf::Event::Closed) {
                window.close();
            }
        }
        float deltaTime = clock.restart().asSeconds();
        ImGui::SFML::Update(window, deltaClock.restart());


        window.clear(sf::Color::Black);


            // Explorer mode UI
        ImGui::Begin("Explorer Mode");
        ImGui::Separator();

        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFpsTime).count() / 1000.0;
        if (elapsedTime >= 0.5) {
            double valid_fps = frameCount / elapsedTime;
            fps = valid_fps;
            frameCount = 0;
            lastFpsTime = currentTime;
        }
        ImGui::Text("FPS: %.1f", fps);

        ImGui::End();
        
        
        float scale = 5.0f;
        float zoomedInLeft = ball.getPosition().x - 16 * scale;
        float zoomedInRight = ball.getPosition().x + 16 * scale;
        float zoomedInTop = ball.getPosition().y - 9 * scale;
        float zoomedInBottom = ball.getPosition().y + 9 * scale;



        sf::View zoomedInView(sf::FloatRect(zoomedInLeft,
            zoomedInTop,
            zoomedInRight - zoomedInLeft,
            zoomedInBottom - zoomedInTop));
        window.setView(zoomedInView);

        renderWalls(window, walls, mutex, 1.0f);
        renderParticles(particles, window, mutex, 1.0f);

        window.draw(ball);
        particles.clear();

        ImGui::SFML::Render(window);

        window.display();

        frameCount++;
    }

    ImGui::SFML::Shutdown();
    inputThread.join();

    // Cleanup and close socket
    closesocket(clientSocket);
    WSACleanup();

    return 0;
}