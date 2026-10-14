#pragma once

#include <QVector3D>
#include <QDebug>
#include <cmath>

// Model/work coordinates stay double until explicitly converted for rendering.
class Coordinate3D
{
public:
    Coordinate3D() = default;
    Coordinate3D(double x, double y, double z) : m_x(x), m_y(y), m_z(z) {}
    Coordinate3D(const QVector3D &point) : m_x(point.x()), m_y(point.y()), m_z(point.z()) {}
    double x() const { return m_x; }
    double y() const { return m_y; }
    double z() const { return m_z; }
    void setX(double value) { m_x = value; }
    void setY(double value) { m_y = value; }
    void setZ(double value) { m_z = value; }
    double operator[](int axis) const { return axis == 0 ? m_x : axis == 1 ? m_y : m_z; }
    double &operator[](int axis) { return axis == 0 ? m_x : axis == 1 ? m_y : m_z; }
    bool isNull() const { return m_x == 0.0 && m_y == 0.0 && m_z == 0.0; }
    double lengthSquared() const { return m_x*m_x + m_y*m_y + m_z*m_z; }
    double length() const { return std::hypot(std::hypot(m_x, m_y), m_z); }
    Coordinate3D normalized() const { const double n = length(); return n > 0.0 ? *this / n : Coordinate3D(); }
    QVector3D toVector3D() const { return QVector3D(float(m_x), float(m_y), float(m_z)); }
    Coordinate3D &operator+=(const Coordinate3D &v) { m_x+=v.x();m_y+=v.y();m_z+=v.z();return *this; }
    Coordinate3D &operator-=(const Coordinate3D &v) { m_x-=v.x();m_y-=v.y();m_z-=v.z();return *this; }
    Coordinate3D &operator*=(double value) { m_x*=value;m_y*=value;m_z*=value;return *this; }
    Coordinate3D &operator/=(double value) { m_x/=value;m_y/=value;m_z/=value;return *this; }
    friend Coordinate3D operator+(Coordinate3D a,const Coordinate3D &b) { return a+=b; }
    friend Coordinate3D operator-(Coordinate3D a,const Coordinate3D &b) { return a-=b; }
    friend Coordinate3D operator-(const Coordinate3D &a) { return {-a.x(),-a.y(),-a.z()}; }
    friend Coordinate3D operator*(Coordinate3D a,double b) { return a*=b; }
    friend Coordinate3D operator*(double b,Coordinate3D a) { return a*=b; }
    friend Coordinate3D operator/(Coordinate3D a,double b) { return a/=b; }
    friend bool operator==(const Coordinate3D &a,const Coordinate3D &b) { return a.x()==b.x()&&a.y()==b.y()&&a.z()==b.z(); }
    friend bool operator!=(const Coordinate3D &a,const Coordinate3D &b) { return !(a==b); }
    static double dotProduct(const Coordinate3D &a,const Coordinate3D &b) { return a.x()*b.x()+a.y()*b.y()+a.z()*b.z(); }
private:
    double m_x=0.0, m_y=0.0, m_z=0.0;
};

inline QDebug operator<<(QDebug debug,const Coordinate3D &point)
{
    QDebugStateSaver save(debug);
    return debug.nospace()<<"Coordinate3D("<<point.x()<<", "<<point.y()<<", "<<point.z()<<")";
}
