#ifndef M_PI
#define M_PI 3.14159265358979323846
#define RADIUS 5.0

#endif
#include <SFML/Graphics.hpp>
#include "imgui.h"
#include "imgui-SFML.h"

#include <SFML/Network.hpp> 

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
#include <stdexcept>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctime>
#include <fstream>
#include <string> // for std::string
#include <sstream> // for std::stringstream

bool devWindowCreated = false;
sf::RenderWindow window;
// Global variables
std::mutex clientSocketsMutex;
std::vector<SOCKET> clientSockets;

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
public:
    Particle(float startX, float startY, float speed, float angle)
        : position(startX, startY), velocity(speed* std::cos(angle), speed* std::sin(angle)), isCollided(false) {}

    Particle(const Particle& other)
        : position(other.position), velocity(other.velocity) {}

    Particle& operator=(const Particle& other) {
        if (this != &other) {
            std::lock(mutex, other.mutex);
            std::lock_guard<std::mutex> self_lock(mutex, std::adopt_lock);
            std::lock_guard<std::mutex> other_lock(other.mutex, std::adopt_lock);
            position = other.position;
            velocity = other.velocity;
        }
        return *this;
    }

    void update(float deltaTime, float canvasWidth, float canvasHeight, const std::vector<sf::VertexArray>& walls) {
        std::lock_guard<std::mutex> lock(mutex);

        sf::Vector2f nextPosition = position + velocity * deltaTime;

        if (nextPosition.x < 0 || nextPosition.x > canvasWidth) {
            velocity.x = -velocity.x;
            nextPosition.x = clamp(nextPosition.x, 0.0f, static_cast<float>(canvasWidth));
        }
        if (nextPosition.y < 0 || nextPosition.y > canvasHeight) {
            velocity.y = -velocity.y;
            nextPosition.y = clamp(nextPosition.y, 0.0f, static_cast<float>(canvasHeight));
        }
        if (!isCollided) {
            for (const auto& wall : walls) {
                for (size_t i = 0; i < wall.getVertexCount() - 1; ++i) {
                    sf::Vector2f p1 = wall[i].position;
                    sf::Vector2f p2 = wall[i + 1].position;
                    if (intersects(position, nextPosition, p1, p2)) {
                        isCollided = true;


                        sf::Vector2f normal = getNormal(p1, p2);

                        float dotProduct = velocity.x * normal.x + velocity.y * normal.y;
                        sf::Vector2f reflection = velocity - 2.0f * dotProduct * normal;
                        nextPosition = getCollisionPoint(position, nextPosition, p1, p2);

                        velocity = reflection;
                        break;
                    }
                }
            }
        }
        else {
            isCollided = false;
        }
        position = nextPosition;
    }

    sf::Vector2f getPosition() const {
        return position;
    }
    sf::Vector2f getVelocity() const {
        return velocity;
    }

    //no longer in use
    void render(sf::RenderWindow& window) const {
        std::lock_guard<std::mutex> lock(mutex);

        sf::CircleShape shape(5.0f);
        shape.setFillColor(sf::Color::Green);
        shape.setPosition(position);
        window.draw(shape);
    }

private:
    sf::Vector2f position;
    sf::Vector2f velocity;
    mutable std::mutex mutex;
    bool isCollided;

    sf::Vector2f getCollisionPoint(const sf::Vector2f& startPos, const sf::Vector2f& endPos, const sf::Vector2f& wallStart, const sf::Vector2f& wallEnd) {
        sf::Vector2f collisionPoint;

        float x1 = startPos.x, y1 = startPos.y;
        float x2 = endPos.x, y2 = endPos.y;
        float x3 = wallStart.x, y3 = wallStart.y;
        float x4 = wallEnd.x, y4 = wallEnd.y;

        float denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);

        if (denom == 0) {
            return endPos;
        }

        float t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
        float u = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / denom;

        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
            collisionPoint.x = x1 + t * (x2 - x1);
            collisionPoint.y = y1 + t * (y2 - y1);
        }
        else {
            return endPos;
        }

        return collisionPoint;
    }

    static bool intersects(const sf::Vector2f& p1, const sf::Vector2f& p2, const sf::Vector2f& q1, const sf::Vector2f& q2) {
        float s1_x, s1_y, s2_x, s2_y;
        s1_x = p2.x - p1.x;
        s1_y = p2.y - p1.y;
        s2_x = q2.x - q1.x;
        s2_y = q2.y - q1.y;
        float s, t;
        s = (-s1_y * (p1.x - q1.x) + s1_x * (p1.y - q1.y)) / (-s2_x * s1_y + s1_x * s2_y);
        t = (s2_x * (p1.y - q1.y) - s2_y * (p1.x - q1.x)) / (-s2_x * s1_y + s1_x * s2_y);

        return s >= 0 && s <= 1 && t >= 0 && t <= 1;
    }

    sf::Vector2f getNormal(const sf::Vector2f& p1, const sf::Vector2f& p2) {
        sf::Vector2f direction = p2 - p1;

        sf::Vector2f normal(-direction.y, direction.x); // Rotate direction vector 90 degrees

        float length = std::sqrt(normal.x * normal.x + normal.y * normal.y);
        if (length != 0) {
            normal.x /= length;
            normal.y /= length;
        }

        return normal;
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

void renderSprite(const std::vector<sf::Vector2f>& receivedPositions,
    std::mutex& mutex,
    sf::RenderWindow& window,
    float scale) {
    std::lock_guard<std::mutex> lock(mutex);

    sf::CircleShape particleShape(5.0f * scale); // Adjust particle size based on scale
    particleShape.setFillColor(sf::Color::Red);

    for (const auto& position : receivedPositions) {
        particleShape.setPosition(position);
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
                return true; // Collision detected
            }
        }
    }
    // Check if the position is outside the canvas boundaries
    if (position.x < 0 || position.x >= canvasWidth || position.y < 0 || position.y >= canvasHeight) {
        return true; // Collision detected with canvas boundaries
    }
    return false; // No collision detected
}

void handleInput(sf::CircleShape& ball, float canvasWidth, float canvasHeight, const std::vector<sf::VertexArray>& walls, bool& developerMode) {
    const float speed = 5.0f;
    std::cout << developerMode << std::endl;
    while (true) {
        if (!developerMode) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::W) && ball.getPosition().y >= 0) {
                sf::Vector2f nextPosition = ball.getPosition();
                nextPosition.y -= speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(0, -speed);
                }
            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::A) && ball.getPosition().x >= 0) {
                sf::Vector2f nextPosition = ball.getPosition();
                nextPosition.x -= speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(-speed, 0);
                }
            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::S) && ball.getPosition().y + ball.getRadius() + RADIUS < canvasHeight) {
                sf::Vector2f nextPosition = ball.getPosition();
                nextPosition.y += speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(0, speed);
                }
            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::D) &&
                ball.getPosition().x + ball.getRadius() + RADIUS < canvasWidth) {
                sf::Vector2f nextPosition = ball.getPosition();
                nextPosition.x += speed;
                if (!collidesWithWalls(nextPosition, walls, canvasWidth, canvasHeight)) {
                    ball.move(speed, 0);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


// Function to receive packets from the client socket
// sf::Vector2f receivePackets(SOCKET clientSocket) {
//     sf::Vector2f receivedPosition;
//     while (true) {
//         // Receive ball position from the client socket
//         if (recv(clientSocket, reinterpret_cast<char*>(&receivedPosition), sizeof(receivedPosition), 0) != SOCKET_ERROR) {
//             // Print the received position
//             std::cout << "Received ball position: (" << receivedPosition.x << ", " << receivedPosition.y << ")" << std::endl;
//             // Update the global variable with the received position
//             return receivedPosition;
//         }
//         else {
//             std::cout << "Connection closed or error occurred." << std::endl;
//             return sf::Vector2f(-1000, -1000);
//         }
//     }
// }

// std::tuple<std::string, sf::Vector2f> receivePackets(SOCKET clientSocket) {
//     std::string clientId;
//     sf::Vector2f receivedPosition;

//     // Receive data from the client socket
//     char idBuffer[256];
//     char buffer[sizeof(idBuffer) + sizeof(sf::Vector2f)];

//     if (recv(clientSocket, buffer, sizeof(buffer), 0) != SOCKET_ERROR) {
//         // Extract client ID and position from buffer
//         std::memcpy(idBuffer, buffer, sizeof(idBuffer));
//         idBuffer[sizeof(idBuffer) - 1] = '\0';
//         clientId = std::string(idBuffer);

//         std::memcpy(&receivedPosition, buffer + sizeof(int), sizeof(sf::Vector2f));

//         // Print the received data
//         std::cout << "Received from client: " << clientId << "Ball position (" << receivedPosition.x << ", " << receivedPosition.y << ")" << std::endl;
//     }
//     else {
//         std::cout << "Connection closed or error occurred." << std::endl;
//         // Return a default client ID and position in case of error
//         clientId = "";
//         receivedPosition = sf::Vector2f(-1000, -1000);
//     }

//     return std::make_tuple(clientId, receivedPosition);
// }    

std::tuple<std::string, sf::Vector2f> receivePackets(SOCKET clientSocket, std::string& id) {
    sf::Vector2f receivedPosition;
    sf::Packet packet;

    while (true) {
        // Receive packet from the client socket
        char buffer[1024]; // Adjust the buffer size according to your needs
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived == SOCKET_ERROR) {
            std::cerr << "Failed to receive data from the client!" << std::endl;
            return std::make_tuple("",sf::Vector2f(-1000, -1000));
        }

        // Load the received data into the packet
        packet.append(buffer, bytesReceived);

        // Extract ID and position from the packet
        packet >> id >> receivedPosition.x >> receivedPosition.y;

        // Return the received position
        return std::make_tuple(id, receivedPosition);
    }
}

std::string receiveSerializedData(SOCKET clientSocket) {
    // Buffer to store received data
    char buffer[1024]; // Adjust buffer size as needed

    // Receive data from the client socket
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);

    if (bytesReceived <= 0) {
        // Handle connection error or closed connection
        return ""; // Return empty string
    }

    // Convert received data to a string
    std::string receivedData(buffer, bytesReceived);

    return receivedData;
}

void sendParticles(SOCKET clientSocket, const std::vector<Particle>& particles) {
    // Send client ID
    /*
    if (send(clientSocket, clientID.c_str(), clientID.size() + 1, 0) == SOCKET_ERROR) {
        std::cerr << "Error sending client ID." << std::endl;
        return;
    }
    */
    // Serialize particle positions
    std::vector<sf::Vector2f> positions;
    positions.reserve(particles.size());
    for (const auto& particle : particles) {
        positions.push_back(particle.getPosition());
    }

    // Send particle positions
    if (send(clientSocket, reinterpret_cast<const char*>(positions.data()), sizeof(sf::Vector2f) * positions.size(), 0) == SOCKET_ERROR) {
        std::cerr << "Error sending particle positions." << std::endl;
        return;
    }
}

void updateBallPositions(const std::vector<sf::Vector2f>& receivedPositions, std::vector<sf::CircleShape>& balls) {
    // Iterate over each received position and update the corresponding ball position
    for (size_t i = 0; i < receivedPositions.size(); ++i) {
        balls[i].setPosition(receivedPositions[i]);
    }
}

void clientHandler(const std::vector<SOCKET>& clientSockets) {
    char buffer[200];
    int byteCount;

    sf::Clock clock;
    sf::Clock deltaClock;
    sf::Clock fpsClock;
    int frameCount = 0;
    float fps = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();

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

    std::vector<sf::CircleShape> balls;
    bool developerMode = true; // Default to developer mode


    std::vector<std::thread> receiveThreads;
    std::vector<sf::Vector2f> receivedPositions(clientSockets.size(), sf::Vector2f(-1000, -1000)); // Initialize received positions
    std::vector<std::string> clientIDs;


    // Start a receive thread for each client socket
    for (size_t i = 0; i < clientSockets.size(); ++i) {
        // Create a new ball object for this client
        sf::CircleShape ball(RADIUS);
        ball.setFillColor(sf::Color::Red);
        ball.setPosition(640, 360); // Initial position
        balls.push_back(ball);

        receiveThreads.emplace_back([&clientSockets, &receivedPositions, &balls, &clientIDs, i]() {
            while (true) {
                // Receive data from the client socket
                std::string serializedData = receiveSerializedData(clientSockets[i]);

                // Deserialize the data
                std::istringstream iss(serializedData);
                std::string id;
                sf::Vector2f position;
                iss >> id >> position.x >> position.y;

                // Update the received position for this client
                receivedPositions[i] = position;

                // Update the position of the corresponding ball
                balls[i].setPosition(position);

                // Store the client ID
                clientIDs.emplace_back(id);

                // Print the received data if needed
                // std::cout << "ID: " << id << ", Position: (" << position.x << ", " << position.y << ")" << std::endl;
            }
            });
    }

    unsigned int numThreads = std::thread::hardware_concurrency();
    ThreadPool threadPool(numThreads);

    // Mutex for synchronization
    std::mutex mutex;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(event);

            if (event.type == sf::Event::Closed) {
                window.close();
            }
            else if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left && developerMode) {
                if (!ImGui::GetIO().WantCaptureMouse) {
                    if (!isDrawingLine) {
                        isDrawingLine = true;
                        lineStart = window.mapPixelToCoords(sf::Vector2i(event.mouseButton.x, event.mouseButton.y));
                    }
                    else {
                        isDrawingLine = false;
                        lineEnd = window.mapPixelToCoords(sf::Vector2i(event.mouseButton.x, event.mouseButton.y));
                        walls.emplace_back(sf::LinesStrip, 2);
                        walls.back()[0].position = lineStart;
                        walls.back()[1].position = lineEnd;
                    }
                }
            }
        }
        float deltaTime = clock.restart().asSeconds();
        ImGui::SFML::Update(window, deltaClock.restart());


        window.clear(sf::Color::Black);

        updateBallPositions(receivedPositions, balls);

        window.setView(window.getDefaultView());

        // Developer mode UI
        ImGui::Begin("Developer Mode");
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

        ImGui::Separator();

        ImGui::End();

        ImGui::Begin("Particle Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Separator();

        if (ImGui::BeginTabBar("Settings Tabs")) {
            if (ImGui::BeginTabItem("Line Setting")) {
                ImGui::SliderFloat("Line Start X", &lineStart.x, 0.0f, canvasWidth);
                ImGui::SliderFloat("Line Start Y", &lineStart.y, 0.0f, canvasHeight);
                ImGui::SliderFloat("Line End X", &lineEnd.x, 0.0f, canvasWidth);
                ImGui::SliderFloat("Line End Y", &lineEnd.y, 0.0f, canvasHeight);
                ImGui::SliderFloat("Velocity", &speed, 50.0f, 500.0f);
                ImGui::SliderFloat("Angle (degrees)", &angle, 0.0f, 360.0f);
                ImGui::SliderInt("Number of Particles", &numParticles, 1, 10000);
                if (ImGui::Button("Generate Particles")) {
                    particles.clear();
                    for (int i = 0; i < numParticles; ++i) {
                        float t = 0.0f;
                        if (numParticles > 1) {
                            t = static_cast<float>(i) / (numParticles - 1);
                        }
                        sf::Vector2f position = lineStart + t * (lineEnd - lineStart);
                        particles.emplace_back(position.x, position.y, speed, angle);
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Angle Setting")) {
                ImGui::SliderFloat("Spawn Point X", &lineStart.x, 0.0f, canvasWidth);
                ImGui::SliderFloat("Spawn Point Y", &lineStart.y, 0.0f, canvasHeight);
                ImGui::SliderAngle("Start Angle", &startAngle);
                ImGui::SliderAngle("End Angle", &endAngle);
                ImGui::SliderFloat("Velocity", &speed, 50.0f, 500.0f);
                ImGui::SliderInt("Number of Particles", &numParticles, 1, 10000);
                if (ImGui::Button("Generate Particles")) {
                    particles.clear();
                    float angleIncrement = 0.0f;
                    if (numParticles > 1) {
                        angleIncrement = (endAngle - startAngle) / (numParticles - 1);
                    }
                    for (int i = 0; i < numParticles; ++i) {
                        float currentAngle = startAngle + i * angleIncrement;
                        sf::Vector2f position = lineStart;
                        particles.emplace_back(position.x, position.y, speed, currentAngle);
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Speed Setting")) {
                ImGui::SliderFloat("Spawn Point X", &lineStart.x, 0.0f, canvasWidth);
                ImGui::SliderFloat("Spawn Point Y", &lineStart.y, 0.0f, canvasHeight);
                ImGui::SliderFloat("Angle (degrees)", &angle, 0.0f, 360.0f);
                ImGui::SliderInt("Number of Particles", &numParticles, 1, 10000);
                if (ImGui::Button("Generate Particles")) {
                    particles.clear();
                    float speedIncrement = 450.0f / numParticles;
                    for (int i = 0; i < numParticles; ++i) {
                        float currentSpeed = 50.0f + i * speedIncrement;
                        sf::Vector2f position = lineStart;
                        particles.emplace_back(position.x, position.y, currentSpeed, angle);

                        // send the particles to the client
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        if (ImGui::Button("Clear Particles")) {
            particles.clear();
        }
        if (ImGui::Button("Clear Walls")) {
            walls.clear();
        }
        if (ImGui::Button("Clear last wall")) {
            if (walls.size() > 0) {
                walls.pop_back();
            }
        }

        ImGui::End();

        threadPool.enqueue([&particles, deltaTime, canvasWidth, canvasHeight, &walls]() {
            for (auto& particle : particles) {
                particle.update(deltaTime, canvasWidth, canvasHeight, walls);
            }
            });

        // Render walls and particles
        renderWalls(window, walls, mutex, 1.0f);
        renderParticles(particles, window, mutex, 1.0f);
        renderSprite(receivedPositions, mutex, window, 1.0f);

        // Draw ball
        for (auto& clientSocket : clientSockets) {
            sendParticles(clientSocket, particles);
        }

        //window.draw(balls);

        ImGui::SFML::Render(window);

        window.display();

        frameCount++;
    }
    ImGui::SFML::Shutdown();
    for (auto& thread : receiveThreads) {
        thread.join();
    }
}

void initializeWindow() {
    // Create Dev Window
    window.create(sf::VideoMode(1280 + 10, 720 + 10), "Particle Bouncing Application");
    window.setFramerateLimit(60);

    ImGui::SFML::Init(window);
}

void createWindow(const std::vector<SOCKET>& clientSockets) {
    // Create Dev Window
    if (!devWindowCreated) {
        initializeWindow();
        devWindowCreated = true;
    }

    // Pass the list of client sockets to clientHandler
    clientHandler(clientSockets);
}



// Function to handle client connections
void acceptClients(SOCKET serverSocket) {
    while (true) {
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            std::cout << "accept failed: " << WSAGetLastError() << std::endl;
        }
        else {
            std::cout << "Client connected" << std::endl;

            // Lock the mutex before modifying shared resources
            std::lock_guard<std::mutex> lock(clientSocketsMutex);
            clientSockets.push_back(clientSocket);
        }
    }
}

int main() {
    // Initialize WSA variables
    WSADATA wsaData;
    int wsaerr;
    WORD wVersionRequested = MAKEWORD(2, 2);
    wsaerr = WSAStartup(wVersionRequested, &wsaData);
    if (wsaerr != 0) {
        std::cout << "The Winsock dll not found!" << std::endl;
        return 0;
    }
    else {
        std::cout << "Server is on" << std::endl << "The Winsock dll found" << std::endl;
        std::cout << "The status: " << wsaData.szSystemStatus << std::endl;
    }

    // Create socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cout << "Error at socket(): " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "Socket online" << std::endl;
    }

    // Bind the socket
    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY; // Bind to all available interfaces
    service.sin_port = htons(55555);
    if (bind(serverSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        std::cout << "bind() failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 0;
    }
    else {
        std::cout << "bind() on" << std::endl;
    }

    // Listen to incoming connections
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "listen(): Error listening on socket: " << WSAGetLastError() << std::endl;
    }
    else {
        std::cout << "listen() is online, waiting for new connections..." << std::endl;
    }

    // Start a thread to accept incoming client connections
    std::thread(acceptClients, serverSocket).detach();

    // Create the window after server initialization
    createWindow(clientSockets);

    // Wait for user input to exit
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();

    // Cleanup and exit
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}