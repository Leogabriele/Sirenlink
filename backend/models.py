from sqlalchemy import Column, Integer, String, Float, DateTime, Boolean, ForeignKey
from sqlalchemy.sql import func
from database import Base, engine

class Hospital(Base):
    __tablename__ = "hospitals"
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    hospital_id = Column(String, unique=True, index=True)
    name = Column(String)
    password_hash = Column(String)
    latitude = Column(Float, default=19.0760)
    longitude = Column(Float, default=72.8777)
    contact = Column(String, nullable=True)
    created_at = Column(DateTime, default=func.now())

class Ambulance(Base):
    __tablename__ = "ambulances"
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    vehicle_number = Column(String, unique=True, index=True) # e.g. MH01AB1234
    hospital_id = Column(String, ForeignKey("hospitals.hospital_id"), nullable=True)
    latitude = Column(Float, default=19.0760)
    longitude = Column(Float, default=72.8777)
    speed = Column(Float, default=0.0)
    status = Column(String, default='idle') # idle/active/emergency/returning
    destination_lat = Column(Float, nullable=True)
    destination_lng = Column(Float, nullable=True)
    last_seen = Column(DateTime, default=func.now(), onupdate=func.now())

class Vehicle(Base):
    __tablename__ = "vehicles"
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    vehicle_number = Column(String, unique=True, index=True) # e.g. MH12CD5678
    latitude = Column(Float, default=19.0760)
    longitude = Column(Float, default=72.8777)
    speed = Column(Float, default=0.0)
    status = Column(String, default='connected')
    last_seen = Column(DateTime, default=func.now(), onupdate=func.now())

class Emergency(Base):
    __tablename__ = "emergencies"
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    ambulance_id = Column(String, ForeignKey("ambulances.vehicle_number"))
    hospital_id = Column(String, nullable=True)
    start_time = Column(DateTime, default=func.now())
    end_time = Column(DateTime, nullable=True)
    status = Column(String, default='active')
    severity = Column(String, default='critical')

class Alert(Base):
    __tablename__ = "alerts"
    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    emergency_id = Column(Integer, ForeignKey("emergencies.id"))
    vehicle_number = Column(String)
    direction = Column(String) # LEFT/RIGHT/HOLD/PULL_OVER
    distance = Column(Float)
    severity = Column(String)
    message = Column(String)
    acknowledged = Column(Boolean, default=False)
    created_at = Column(DateTime, default=func.now())

# Auto-create tables on import
Base.metadata.create_all(bind=engine)
